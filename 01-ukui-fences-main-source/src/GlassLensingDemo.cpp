#include "DesktopCanvas.h"
#include "FenceWidget.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QPointer>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVector2D>
#include <QtMath>

#include <memory>

namespace {

constexpr int kMetricsIntervalMs = 5000;
constexpr int kAnimationIntervalMs = 33;
constexpr qreal kBaseLensStrengthPx = 7.5;
constexpr qreal kVelocityBoostPx = 9.0;
constexpr qreal kEdgeBandPx = 24.0;
constexpr qreal kCornerRadiusPx = 10.0;

bool demoEnabled()
{
    const QByteArray value = qgetenv("UKUI_FENCES_LENSING_DEMO").trimmed().toLower();
    return value.isEmpty() || (value != "0" && value != "false" && value != "off");
}

QString glassLogPath()
{
    const QByteArray overridePath = qgetenv("UKUI_FENCES_GLASS_LOG").trimmed();
    if (!overridePath.isEmpty())
        return QString::fromLocal8Bit(overridePath);

    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty())
        root = QDir::homePath() + QStringLiteral("/.local/share/ukui-fences");
    return root + QStringLiteral("/logs/liquid-glass-demo.jsonl");
}

void writeGlassLog(const QString &event, const QJsonObject &fields = {})
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    const QString path = glassLogPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QJsonObject root = fields;
    root.insert(QStringLiteral("ts"),
                QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("event"), event);
    root.insert(QStringLiteral("pid"),
                static_cast<double>(QCoreApplication::applicationPid()));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.write("\n");
}

struct FenceMotionState {
    QPoint lastPos;
    QVector2D velocity;
    qreal lensStrength = kBaseLensStrengthPx;
    qreal peakVelocity = 0.0;
    qreal peakLensStrength = kBaseLensStrengthPx;
    bool initialized = false;
};

} // namespace

class DesktopLensingOverlay final : public QOpenGLWidget,
                                    protected QOpenGLFunctions
{
public:
    explicit DesktopLensingOverlay(DesktopCanvas *canvas)
        : QOpenGLWidget(canvas), m_canvas(canvas)
    {
        setObjectName(QStringLiteral("ukui-fences-lensing-overlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_AlwaysStackOnTop, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);

        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(2, 1);
        format.setProfile(QSurfaceFormat::NoProfile);
        format.setAlphaBufferSize(8);
        format.setDepthBufferSize(0);
        format.setStencilBufferSize(0);
        setFormat(format);

        resize(canvas->size());
        show();
        raise();
        m_uptime.start();
        m_motionClock.start();

        m_animationTimer.setInterval(kAnimationIntervalMs);
        m_animationTimer.setTimerType(Qt::CoarseTimer);
        connect(&m_animationTimer, &QTimer::timeout, this, [this] {
            updateMotionState();
            update();
        });
        m_animationTimer.start();

        m_metricsTimer.setInterval(kMetricsIntervalMs);
        m_metricsTimer.setTimerType(Qt::CoarseTimer);
        connect(&m_metricsTimer, &QTimer::timeout, this, [this] {
            flushMetrics();
        });
        m_metricsTimer.start();

        QTimer::singleShot(1200, this, [this] {
            if (!isValid()) {
                QJsonObject fields;
                fields.insert(QStringLiteral("fallback"),
                              QStringLiteral("qopenglwidget_invalid"));
                writeGlassLog(QStringLiteral("opengl_unavailable"), fields);
                m_animationTimer.stop();
                hide();
            }
        });

        QJsonObject fields;
        fields.insert(QStringLiteral("width"), canvas->width());
        fields.insert(QStringLiteral("height"), canvas->height());
        writeGlassLog(QStringLiteral("overlay_created"), fields);
    }

    ~DesktopLensingOverlay() override
    {
        flushMetrics();
        if (m_texture && context() && context()->isValid()) {
            makeCurrent();
            m_texture.reset();
            doneCurrent();
        }
        writeGlassLog(QStringLiteral("overlay_destroyed"));
    }

    void syncToCanvas()
    {
        if (!m_canvas)
            return;
        if (size() != m_canvas->size())
            resize(m_canvas->size());
        raise();
        updateMotionState();
        update();
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        static const char *vertexShader = R"GLSL(
            attribute vec2 a_position;
            varying vec2 v_uv;
            void main()
            {
                v_uv = (a_position + vec2(1.0)) * 0.5;
                gl_Position = vec4(a_position, 0.0, 1.0);
            }
        )GLSL";

        static const char *fragmentShader = R"GLSL(
            #ifdef GL_ES
            precision mediump float;
            #endif

            varying vec2 v_uv;
            uniform sampler2D u_wallpaper;
            uniform vec2 u_wallpaperSize;
            uniform vec2 u_widgetSize;
            uniform vec2 u_widgetOrigin;
            uniform vec2 u_cursor;
            uniform vec2 u_velocity;
            uniform float u_edgeBand;
            uniform float u_cornerRadius;
            uniform float u_lensStrength;
            uniform float u_time;

            float sdRoundBox(vec2 p, vec2 halfSize, float radius)
            {
                vec2 q = abs(p) - (halfSize - vec2(radius));
                return length(max(q, vec2(0.0)))
                     + min(max(q.x, q.y), 0.0) - radius;
            }

            vec2 sdfNormal(vec2 local)
            {
                vec2 center = u_widgetSize * 0.5;
                vec2 halfSize = max(vec2(1.0), center - vec2(0.75));
                vec2 p = local - center;
                float e = 1.25;
                float dx = sdRoundBox(p + vec2(e, 0.0), halfSize, u_cornerRadius)
                         - sdRoundBox(p - vec2(e, 0.0), halfSize, u_cornerRadius);
                float dy = sdRoundBox(p + vec2(0.0, e), halfSize, u_cornerRadius)
                         - sdRoundBox(p - vec2(0.0, e), halfSize, u_cornerRadius);
                vec2 n = vec2(dx, dy);
                return length(n) > 0.0001 ? normalize(n) : vec2(0.0, -1.0);
            }

            void main()
            {
                vec2 local = vec2(v_uv.x * u_widgetSize.x,
                                  (1.0 - v_uv.y) * u_widgetSize.y);
                vec2 center = u_widgetSize * 0.5;
                vec2 halfSize = max(vec2(1.0), center - vec2(0.75));
                float sdf = sdRoundBox(local - center, halfSize, u_cornerRadius);
                if (sdf > 0.5) {
                    gl_FragColor = vec4(0.0);
                    return;
                }

                float insideDistance = max(0.0, -sdf);
                float edge = 1.0 - smoothstep(0.0, u_edgeBand, insideDistance);
                if (edge <= 0.002) {
                    gl_FragColor = vec4(0.0);
                    return;
                }

                vec2 normal = sdfNormal(local);
                float velocity = clamp(length(u_velocity) / 1800.0, 0.0, 1.0);
                float pulse = 0.96 + 0.04 * sin(u_time * 2.2);
                float displacement = u_lensStrength * edge * edge
                                   * (1.0 + velocity * 0.65) * pulse;
                vec2 screenPx = u_widgetOrigin + local;
                vec2 baseUv = screenPx / max(u_wallpaperSize, vec2(1.0));
                baseUv.y = 1.0 - baseUv.y;
                vec2 offset = normal * displacement
                            / max(u_wallpaperSize, vec2(1.0));
                offset.y = -offset.y;

                vec2 chroma = offset * 0.12;
                float r = texture2D(u_wallpaper,
                    clamp(baseUv + offset + chroma, 0.0, 1.0)).r;
                vec4 gb = texture2D(u_wallpaper,
                    clamp(baseUv + offset, 0.0, 1.0));
                float b = texture2D(u_wallpaper,
                    clamp(baseUv + offset - chroma, 0.0, 1.0)).b;
                vec3 refracted = vec3(r, gb.g, b);

                vec2 cursorDelta = local - u_cursor;
                float cursorGlow = exp(-dot(cursorDelta, cursorDelta) / 9500.0);
                vec2 lightDir = normalize(vec2(-0.55, -0.84)
                                          + clamp(u_velocity / 2600.0,
                                                  vec2(-0.25), vec2(0.25)));
                float specular = pow(max(0.0, dot(-normal, lightDir)), 6.0);
                specular = specular * (0.18 + 0.22 * edge)
                         + cursorGlow * edge * 0.16;
                float topBand = 1.0;
                if (normal.y < -0.65)
                    topBand = mix(0.48, 0.82,
                                  clamp(local.y / 10.0, 0.0, 1.0));
                vec3 finalColor = refracted + vec3(specular);
                float alpha = edge * (0.38 + 0.34 * edge) * topBand;
                gl_FragColor = vec4(finalColor, clamp(alpha, 0.0, 0.82));
            }
        )GLSL";

        bool ok = m_program.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                                     vertexShader);
        if (!ok) {
            logShaderFailure(QStringLiteral("vertex_compile"));
            disableOverlay();
            return;
        }
        ok = m_program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                               fragmentShader);
        if (!ok) {
            logShaderFailure(QStringLiteral("fragment_compile"));
            disableOverlay();
            return;
        }
        ok = m_program.link();
        if (!ok) {
            logShaderFailure(QStringLiteral("program_link"));
            disableOverlay();
            return;
        }

        m_program.bind();
        m_positionAttribute = m_program.attributeLocation("a_position");
        m_program.release();
        if (m_positionAttribute < 0) {
            writeGlassLog(QStringLiteral("shader_attribute_missing"));
            disableOverlay();
            return;
        }

        QJsonObject fields;
        const auto glText = [this](GLenum name) {
            const GLubyte *value = glGetString(name);
            return value
                ? QString::fromLatin1(reinterpret_cast<const char *>(value))
                : QString();
        };
        if (QOpenGLContext *ctx = context()) {
            fields.insert(QStringLiteral("gl_version"), glText(GL_VERSION));
            fields.insert(QStringLiteral("gl_renderer"), glText(GL_RENDERER));
            fields.insert(QStringLiteral("gl_vendor"), glText(GL_VENDOR));
            fields.insert(QStringLiteral("format_major"),
                          ctx->format().majorVersion());
            fields.insert(QStringLiteral("format_minor"),
                          ctx->format().minorVersion());
            fields.insert(QStringLiteral("alpha_buffer_bits"),
                          ctx->format().alphaBufferSize());
        }
        writeGlassLog(QStringLiteral("shader_ready"), fields);
        m_shaderReady = true;
    }

    void resizeGL(int width, int height) override
    {
        glViewport(0, 0, width, height);
    }

    void paintGL() override
    {
        QElapsedTimer submitTimer;
        submitTimer.start();
        const qreal dpr = devicePixelRatioF();
        glViewport(0, 0, qRound(width() * dpr), qRound(height() * dpr));
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (!m_shaderReady || !m_canvas || width() <= 0 || height() <= 0)
            return;
        if (!ensureTexture())
            return;

        static const GLfloat vertices[] = {
            -1.f, -1.f,
             1.f, -1.f,
            -1.f,  1.f,
             1.f,  1.f
        };

        m_program.bind();
        m_texture->bind(0);
        m_program.setUniformValue("u_wallpaper", 0);
        m_program.setUniformValue(
            "u_wallpaperSize",
            QVector2D(m_textureSize.width(), m_textureSize.height()));
        m_program.setUniformValue("u_edgeBand",
                                  static_cast<GLfloat>(kEdgeBandPx));
        m_program.setUniformValue("u_cornerRadius",
                                  static_cast<GLfloat>(kCornerRadiusPx));
        m_program.setUniformValue(
            "u_time", static_cast<GLfloat>(m_uptime.elapsed() / 1000.0));
        m_program.enableAttributeArray(m_positionAttribute);
        m_program.setAttributeArray(m_positionAttribute, GL_FLOAT, vertices, 2);

        int drawnFences = 0;
        qreal maxSpeed = 0.0;
        const QPoint cursorCanvas = m_canvas->mapFromGlobal(QCursor::pos());
        for (FenceWidget *fence : m_canvas->m_fences) {
            if (!fence || !fence->isVisible() ||
                fence->width() < 2 || fence->height() < 2)
                continue;

            const QRect geo = fence->geometry().intersected(m_canvas->rect());
            if (geo.width() < 2 || geo.height() < 2)
                continue;

            const FenceMotionState state = m_motion.value(fence->fenceId());
            const qreal speed = state.velocity.length();
            maxSpeed = qMax(maxSpeed, speed);
            const int viewportY = height() - geo.y() - geo.height();
            glViewport(qRound(geo.x() * dpr),
                       qRound(viewportY * dpr),
                       qRound(geo.width() * dpr),
                       qRound(geo.height() * dpr));
            m_program.setUniformValue(
                "u_widgetSize", QVector2D(fence->width(), fence->height()));
            m_program.setUniformValue(
                "u_widgetOrigin", QVector2D(fence->x(), fence->y()));
            m_program.setUniformValue(
                "u_cursor", QVector2D(cursorCanvas.x() - fence->x(),
                                      cursorCanvas.y() - fence->y()));
            m_program.setUniformValue("u_velocity", state.velocity);
            m_program.setUniformValue(
                "u_lensStrength", static_cast<GLfloat>(state.lensStrength));
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            ++drawnFences;
        }

        m_program.disableAttributeArray(m_positionAttribute);
        m_texture->release();
        m_program.release();
        glViewport(0, 0, qRound(width() * dpr), qRound(height() * dpr));

        const qreal submitMs = submitTimer.nsecsElapsed() / 1000000.0;
        ++m_frameCount;
        m_submitMsTotal += submitMs;
        m_submitMsMax = qMax(m_submitMsMax, submitMs);
        m_lastDrawnFenceCount = drawnFences;
        m_lastMaxSpeed = maxSpeed;
    }

private:
    void disableOverlay()
    {
        m_shaderReady = false;
        m_animationTimer.stop();
        hide();
    }

    void logShaderFailure(const QString &stage)
    {
        QJsonObject fields;
        fields.insert(QStringLiteral("stage"), stage);
        fields.insert(QStringLiteral("log"), m_program.log().left(4096));
        writeGlassLog(QStringLiteral("shader_failed"), fields);
    }

    qint64 wallpaperSourceKey() const
    {
        if (!m_canvas || m_canvas->m_wallpaperCache.isNull())
            return 0;
        qint64 key = m_canvas->m_wallpaperCache.cacheKey();
        key ^= (static_cast<qint64>(m_canvas->width()) << 32);
        key ^= static_cast<qint64>(m_canvas->height());
        return key;
    }

    bool ensureTexture()
    {
        const qint64 sourceKey = wallpaperSourceKey();
        if (sourceKey == 0) {
            if (!m_reportedMissingWallpaper) {
                writeGlassLog(QStringLiteral("wallpaper_missing"));
                m_reportedMissingWallpaper = true;
            }
            return false;
        }
        m_reportedMissingWallpaper = false;
        if (m_texture && m_textureSourceKey == sourceKey)
            return true;

        const QImage wallpaper = m_canvas->renderedWallpaperImage();
        if (wallpaper.isNull()) {
            writeGlassLog(QStringLiteral("wallpaper_render_failed"));
            return false;
        }

        m_texture.reset();
        QImage textureImage = wallpaper.convertToFormat(QImage::Format_RGBA8888)
                                      .mirrored();
        auto texture = std::make_unique<QOpenGLTexture>(textureImage);
        if (!texture->isCreated()) {
            writeGlassLog(QStringLiteral("texture_create_failed"));
            return false;
        }
        texture->setMinificationFilter(QOpenGLTexture::Linear);
        texture->setMagnificationFilter(QOpenGLTexture::Linear);
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_texture = std::move(texture);
        m_textureSourceKey = sourceKey;
        m_textureSize = wallpaper.size();

        QJsonObject fields;
        fields.insert(QStringLiteral("wallpaper_width"), wallpaper.width());
        fields.insert(QStringLiteral("wallpaper_height"), wallpaper.height());
        fields.insert(QStringLiteral("source_key"), QString::number(sourceKey));
        writeGlassLog(QStringLiteral("texture_uploaded"), fields);
        return true;
    }

    void updateMotionState()
    {
        if (!m_canvas || !m_motionClock.isValid())
            return;
        const qint64 elapsedMs = qMax<qint64>(1, m_motionClock.restart());
        const qreal scale = 1000.0 / elapsedMs;
        QHash<QString, bool> seen;

        for (FenceWidget *fence : m_canvas->m_fences) {
            if (!fence)
                continue;
            const QString id = fence->fenceId();
            seen.insert(id, true);
            FenceMotionState &state = m_motion[id];
            const QPoint pos = fence->pos();
            if (!state.initialized) {
                state.lastPos = pos;
                state.initialized = true;
            }
            const QPoint delta = pos - state.lastPos;
            const QVector2D instantaneous(delta.x() * scale,
                                          delta.y() * scale);
            state.velocity = state.velocity * 0.72f + instantaneous * 0.28f;
            state.lastPos = pos;
            const qreal speed = qMin<qreal>(1.0,
                                           state.velocity.length() / 1400.0);
            const qreal target = kBaseLensStrengthPx
                               + kVelocityBoostPx * speed;
            state.lensStrength = state.lensStrength * 0.82 + target * 0.18;
            state.peakVelocity = qMax<qreal>(state.peakVelocity,
                                            state.velocity.length());
            state.peakLensStrength = qMax(state.peakLensStrength,
                                          state.lensStrength);
        }

        const auto keys = m_motion.keys();
        for (const QString &key : keys) {
            if (!seen.contains(key))
                m_motion.remove(key);
        }
    }

    void flushMetrics()
    {
        if (m_frameCount <= 0)
            return;
        QJsonObject fields;
        fields.insert(QStringLiteral("frames"), m_frameCount);
        fields.insert(QStringLiteral("avg_submit_ms"),
                      m_submitMsTotal / m_frameCount);
        fields.insert(QStringLiteral("max_submit_ms"), m_submitMsMax);
        fields.insert(QStringLiteral("drawn_fences"), m_lastDrawnFenceCount);
        qreal intervalMaxSpeed = 0.0;
        qreal intervalMaxLensStrength = kBaseLensStrengthPx;
        fields.insert(QStringLiteral("wallpaper_width"), m_textureSize.width());
        fields.insert(QStringLiteral("wallpaper_height"),
                      m_textureSize.height());

        QJsonArray fenceStates;
        if (m_canvas) {
            for (FenceWidget *fence : m_canvas->m_fences) {
                if (!fence)
                    continue;
                const FenceMotionState state =
                    m_motion.value(fence->fenceId());
                QJsonObject item;
                item.insert(QStringLiteral("id"), fence->fenceId());
                item.insert(QStringLiteral("x"), fence->x());
                item.insert(QStringLiteral("y"), fence->y());
                item.insert(QStringLiteral("width"), fence->width());
                item.insert(QStringLiteral("height"), fence->height());
                item.insert(QStringLiteral("velocity_px_s"),
                            state.velocity.length());
                item.insert(QStringLiteral("peak_velocity_px_s"),
                            state.peakVelocity);
                item.insert(QStringLiteral("lens_strength_px"),
                            state.lensStrength);
                item.insert(QStringLiteral("peak_lens_strength_px"),
                            state.peakLensStrength);
                fenceStates.append(item);
                intervalMaxSpeed = qMax(intervalMaxSpeed, state.peakVelocity);
                intervalMaxLensStrength = qMax(intervalMaxLensStrength,
                                               state.peakLensStrength);

                FenceMotionState &mutableState = m_motion[fence->fenceId()];
                mutableState.peakVelocity = mutableState.velocity.length();
                mutableState.peakLensStrength = mutableState.lensStrength;
            }
        }
        fields.insert(QStringLiteral("current_max_velocity_px_s"),
                      m_lastMaxSpeed);
        fields.insert(QStringLiteral("max_velocity_px_s"), intervalMaxSpeed);
        fields.insert(QStringLiteral("max_lens_strength_px"),
                      intervalMaxLensStrength);
        fields.insert(QStringLiteral("fences"), fenceStates);
        writeGlassLog(QStringLiteral("render_metrics"), fields);

        m_frameCount = 0;
        m_submitMsTotal = 0.0;
        m_submitMsMax = 0.0;
    }

    QPointer<DesktopCanvas> m_canvas;
    QOpenGLShaderProgram m_program;
    std::unique_ptr<QOpenGLTexture> m_texture;
    QTimer m_animationTimer;
    QTimer m_metricsTimer;
    QElapsedTimer m_uptime;
    QElapsedTimer m_motionClock;
    QHash<QString, FenceMotionState> m_motion;
    QSize m_textureSize;
    qint64 m_textureSourceKey = 0;
    int m_positionAttribute = -1;
    int m_frameCount = 0;
    int m_lastDrawnFenceCount = 0;
    qreal m_submitMsTotal = 0.0;
    qreal m_submitMsMax = 0.0;
    qreal m_lastMaxSpeed = 0.0;
    bool m_shaderReady = false;
    bool m_reportedMissingWallpaper = false;
};

namespace {

class GlassLensingController final : public QObject
{
public:
    explicit GlassLensingController(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *canvas = qobject_cast<DesktopCanvas *>(watched);
        if (!canvas)
            return QObject::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Polish:
            ensureOverlay(canvas);
            break;
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::LayoutRequest:
            if (auto *overlay = overlayFor(canvas))
                overlay->syncToCanvas();
            break;
        case QEvent::Hide:
            if (auto *overlay = overlayFor(canvas))
                overlay->hide();
            break;
        case QEvent::Destroy:
            m_overlays.remove(canvas);
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    DesktopLensingOverlay *overlayFor(DesktopCanvas *canvas) const
    {
        const auto it = m_overlays.constFind(canvas);
        return it == m_overlays.constEnd() ? nullptr : it.value().data();
    }

    void ensureOverlay(DesktopCanvas *canvas)
    {
        if (!canvas)
            return;
        if (auto *overlay = overlayFor(canvas)) {
            overlay->show();
            overlay->syncToCanvas();
            return;
        }

        auto *overlay = new DesktopLensingOverlay(canvas);
        m_overlays.insert(canvas, overlay);
        connect(canvas, &QObject::destroyed, this, [this, canvas] {
            m_overlays.remove(canvas);
        });
        overlay->syncToCanvas();
    }

    QHash<DesktopCanvas *, QPointer<DesktopLensingOverlay>> m_overlays;
};

void installFenceLensingDemo()
{
    if (!demoEnabled()) {
        writeGlassLog(QStringLiteral("demo_disabled"));
        return;
    }
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;

    auto *controller = new GlassLensingController(app);
    app->installEventFilter(controller);

    QJsonObject fields;
    fields.insert(QStringLiteral("log_path"), glassLogPath());
    fields.insert(QStringLiteral("frame_interval_ms"), kAnimationIntervalMs);
    fields.insert(QStringLiteral("edge_band_px"), kEdgeBandPx);
    fields.insert(QStringLiteral("base_lens_strength_px"),
                  kBaseLensStrengthPx);
    fields.insert(QStringLiteral("velocity_boost_px"), kVelocityBoostPx);
    writeGlassLog(QStringLiteral("demo_installed"), fields);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(installFenceLensingDemo)
