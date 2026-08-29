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
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVector2D>
#include <QWidget>
#include <QtMath>

#include <array>
#include <memory>

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif

namespace {

constexpr int kMetricsIntervalMs = 5000;
constexpr int kGpuQueryCount = 4;
constexpr int kMagneticContourSamples = 64;

qreal envReal(const char *name, qreal fallback, qreal minimum, qreal maximum)
{
    bool ok = false;
    const qreal parsed = qgetenv(name).trimmed().toDouble(&ok);
    return ok ? qBound(minimum, parsed, maximum) : fallback;
}

int envInt(const char *name, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int parsed = qgetenv(name).trimmed().toInt(&ok);
    return ok ? qBound(minimum, parsed, maximum) : fallback;
}

bool envBool(const char *name, bool fallback)
{
    const QByteArray value = qgetenv(name).trimmed().toLower();
    if (value.isEmpty())
        return fallback;
    if (value == "0" || value == "false" || value == "off" || value == "no")
        return false;
    if (value == "1" || value == "true" || value == "on" || value == "yes")
        return true;
    return fallback;
}

struct DemoConfig {
    qreal baseLensStrengthPx = 6.25;
    qreal velocityBoostPx = 10.0;
    qreal velocityNormPxS = 700.0;
    qreal response = 0.35;
    qreal rimBandPx = 8.0;
    qreal edgeBandPx = 28.0;
    qreal specularGain = 1.38;
    qreal cornerRadiusPx = 10.0;
    int activeFrameMs = 33;
    bool gpuTimer = true;
};

const DemoConfig &demoConfig()
{
    static const DemoConfig config = [] {
        DemoConfig c;
        c.baseLensStrengthPx = envReal(
            "UKUI_FENCES_GLASS_BASE_LENS_PX", c.baseLensStrengthPx, 0.0, 30.0);
        c.velocityBoostPx = envReal(
            "UKUI_FENCES_GLASS_VELOCITY_BOOST_PX", c.velocityBoostPx, 0.0, 30.0);
        c.velocityNormPxS = envReal(
            "UKUI_FENCES_GLASS_VELOCITY_NORM_PX_S", c.velocityNormPxS, 100.0, 4000.0);
        c.response = envReal(
            "UKUI_FENCES_GLASS_RESPONSE", c.response, 0.05, 1.0);
        c.rimBandPx = envReal(
            "UKUI_FENCES_GLASS_RIM_BAND_PX", c.rimBandPx, 2.0, 20.0);
        c.edgeBandPx = envReal(
            "UKUI_FENCES_GLASS_EDGE_BAND_PX", c.edgeBandPx, 8.0, 64.0);
        c.edgeBandPx = qMax(c.edgeBandPx, c.rimBandPx + 2.0);
        c.specularGain = envReal(
            "UKUI_FENCES_GLASS_SPECULAR_GAIN", c.specularGain, 0.0, 3.0);
        c.activeFrameMs = envInt(
            "UKUI_FENCES_GLASS_ACTIVE_FRAME_MS", c.activeFrameMs, 16, 100);
        c.gpuTimer = envBool("UKUI_FENCES_GLASS_GPU_TIMER", c.gpuTimer);
        return c;
    }();
    return config;
}

bool demoEnabled()
{
    const QByteArray value = qgetenv("UKUI_FENCES_LENSING_DEMO").trimmed().toLower();
    return value == "1" || value == "true" ||
           value == "on" || value == "yes";
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
    qreal lensStrength = demoConfig().baseLensStrengthPx;
    qreal peakVelocity = 0.0;
    qreal peakLensStrength = demoConfig().baseLensStrengthPx;
    bool initialized = false;
};

struct GpuQuerySlot {
    GLuint id = 0;
    bool pending = false;
};

} // namespace

class DesktopLensingOverlay final : public QOpenGLWidget,
                                    protected QOpenGLFunctions
{
public:
    explicit DesktopLensingOverlay(DesktopCanvas *canvas)
        : QOpenGLWidget(canvas)
        , m_canvas(canvas)
        , m_config(demoConfig())
    {
        setObjectName(QStringLiteral("ukui-fences-lensing-overlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_AlwaysStackOnTop, true);
        setAttribute(Qt::WA_NoSystemBackground, false);
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

        const QRect initialBounds = overlayBounds();
        m_canvasOrigin = initialBounds.topLeft();
        setGeometry(initialBounds);
        connect(canvas, &DesktopCanvas::wallpaperCacheChanged,
                this, [this] {
            m_textureSourceKey = 0;
            writeGlassLog(QStringLiteral("wallpaper_cache_changed"));
            // Refresh is an explicit, rare action.  Force one synchronous GL
            // frame here so an obscured/unfocused desktop cannot coalesce the
            // update away; normal mouse/motion frames remain event-driven.
            QTimer::singleShot(0, this, [this] {
                if (!m_canvas || !isVisible())
                    return;
                updateMotionState();
                repaint();
            });
        });
        show();
        // Qt composites a transparent QOpenGLWidget with sibling widgets only
        // in AlwaysStackOnTop mode.  The shader's center is fully transparent,
        // so Fence content remains untouched while the outward band is visible.
        m_motionClock.start();
        m_frameThrottleClock.start();

        m_animationTimer.setTimerType(Qt::CoarseTimer);
        m_animationTimer.setInterval(m_config.activeFrameMs);
        connect(&m_animationTimer, &QTimer::timeout, this, [this] {
            const bool active = updateMotionState();
            update();
            if (!active)
                m_animationTimer.stop();
        });
        m_animationTimer.start();

        m_metricsTimer.setInterval(kMetricsIntervalMs);
        m_metricsTimer.setTimerType(Qt::CoarseTimer);
        connect(&m_metricsTimer, &QTimer::timeout, this, [this] {
            flushMetrics();
        });
        m_metricsTimer.start();

        QTimer::singleShot(1200, this, [this] {
            if (isVisible() && !isValid()) {
                QJsonObject fields;
                fields.insert(QStringLiteral("fallback"),
                              QStringLiteral("qopenglwidget_invalid"));
                writeGlassLog(QStringLiteral("opengl_unavailable"), fields);
                m_animationTimer.stop();
                hide();
            }
        });

        QJsonObject fields;
        fields.insert(QStringLiteral("canvas_width"), canvas->width());
        fields.insert(QStringLiteral("canvas_height"), canvas->height());
        fields.insert(QStringLiteral("overlay_width"), width());
        fields.insert(QStringLiteral("overlay_height"), height());
        fields.insert(QStringLiteral("render_mode"),
                      QStringLiteral("active_fence_edge_only"));
        fields.insert(QStringLiteral("active_frame_ms"), m_config.activeFrameMs);
        fields.insert(QStringLiteral("idle_refresh"),
                      QStringLiteral("event_driven"));
        writeGlassLog(QStringLiteral("overlay_created"), fields);

        // DesktopCanvas maps itself before loading and blurring the wallpaper.
        // Creating a native QOpenGLWidget from that first Show event may paint
        // synchronously while the cache is still empty.  Retry after the
        // canvas constructor/event burst completes; the timer stops again as
        // soon as the static frame has been submitted.
        QTimer::singleShot(0, this, [this] { requestInteractiveFrame(); });
        QTimer::singleShot(250, this, [this] { requestInteractiveFrame(); });
    }

    ~DesktopLensingOverlay() override
    {
        flushMetrics();
        if (context() && context()->isValid()) {
            makeCurrent();
            destroyGpuQueries();
            m_texture.reset();
            doneCurrent();
        }
        writeGlassLog(QStringLiteral("overlay_destroyed"));
    }

    void syncToCanvas()
    {
        if (!m_canvas)
            return;
        const QRect canvasGeometry = overlayBounds();
        if (!canvasGeometry.isValid() || canvasGeometry.isEmpty()) {
            hide();
            return;
        }
        m_canvasOrigin = canvasGeometry.topLeft();
        if (geometry() != canvasGeometry)
            setGeometry(canvasGeometry);
        if (!isVisible() && m_canvas->isVisible())
            show();
        requestInteractiveFrame();
    }

    void requestInteractiveFrame()
    {
        if (!m_canvas || !isVisible())
            return;

        if (!m_animationTimer.isActive()) {
            m_animationTimer.setInterval(m_config.activeFrameMs);
            m_animationTimer.start();
        }

        // Coalesce mouse storms instead of waking the weak CPU per event.
        if (!m_frameThrottleClock.isValid() ||
            m_frameThrottleClock.elapsed() >= m_config.activeFrameMs) {
            updateMotionState();
            update();
        }
    }

    void activateFence(FenceWidget *fence)
    {
        if (!fence || !m_canvas || !fence->isVisible())
            return;
        m_activeFenceId = fence->fenceId();
        syncToCanvas();
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
            uniform vec2 u_drawSize;
            uniform vec2 u_drawOffset;
            uniform vec2 u_cursor;
            uniform vec2 u_velocity;
            uniform float u_rimBand;
            uniform float u_edgeBand;
            uniform float u_cornerRadius;
            uniform float u_lensStrength;
            uniform float u_specularGain;
            uniform int u_magneticEdge;
            uniform int u_contourCount;
            uniform float u_contourAxisMin;
            uniform float u_contourAxisMax;
            uniform float u_contourCoordinate[64];

            float sdRoundBox(vec2 p, vec2 halfSize, float radius)
            {
                vec2 q = abs(p) - (halfSize - vec2(radius));
                return length(max(q, vec2(0.0)))
                     + min(max(q.x, q.y), 0.0) - radius;
            }

            float roundedSignedDistance(vec2 local)
            {
                vec2 center = u_widgetSize * 0.5;
                vec2 halfSize = max(vec2(1.0), center - vec2(0.75));
                return sdRoundBox(
                    local - center, halfSize, u_cornerRadius);
            }

            vec2 roundedNormal(vec2 local)
            {
                vec2 center = u_widgetSize * 0.5;
                vec2 halfSize = max(vec2(1.0), center - vec2(0.75));
                vec2 p = local - center;
                vec2 q = abs(p) - (halfSize - vec2(u_cornerRadius));
                vec2 corner = max(q, vec2(0.0));
                if (length(corner) > 0.001)
                    return normalize(corner * sign(p));
                return q.x > q.y
                    ? vec2(sign(p.x), 0.0)
                    : vec2(0.0, sign(p.y));
            }

            void magneticData(vec2 local,
                              out float distance,
                              out vec2 normal)
            {
                if (u_magneticEdge == 0 || u_contourCount < 2) {
                    distance = -100000.0;
                    normal = vec2(0.0, -1.0);
                    return;
                }

                bool vertical = u_magneticEdge == 1 || u_magneticEdge == 2;
                float axis = vertical ? local.y : local.x;
                float span = max(1.0, u_contourAxisMax - u_contourAxisMin);
                float samplePosition = clamp(
                    (axis - u_contourAxisMin) / span, 0.0, 1.0) * 63.0;
                int sampleA = int(floor(samplePosition));
                int sampleB = sampleA < 63 ? sampleA + 1 : 63;
                float edgeCoordinate = mix(
                    u_contourCoordinate[sampleA],
                    u_contourCoordinate[sampleB],
                    fract(samplePosition));
                float axisStep = span / 63.0;
                float slope = (u_contourCoordinate[sampleB]
                             - u_contourCoordinate[sampleA])
                            / max(axisStep, 0.001);

                if (u_magneticEdge == 1) {
                    distance = edgeCoordinate - local.x;
                    normal = normalize(vec2(-1.0, slope));
                } else if (u_magneticEdge == 2) {
                    distance = local.x - edgeCoordinate;
                    normal = normalize(vec2(1.0, -slope));
                } else if (u_magneticEdge == 3) {
                    distance = edgeCoordinate - local.y;
                    normal = normalize(vec2(slope, -1.0));
                } else {
                    distance = local.y - edgeCoordinate;
                    normal = normalize(vec2(-slope, 1.0));
                }
            }

            void main()
            {
                vec2 local = u_drawOffset
                           + vec2(v_uv.x * u_drawSize.x,
                                  (1.0 - v_uv.y) * u_drawSize.y);
                vec2 screenPx = u_widgetOrigin + local;
                vec2 baseUv = screenPx / max(u_wallpaperSize, vec2(1.0));
                baseUv.y = 1.0 - baseUv.y;

                float roundedDistance = roundedSignedDistance(local);
                float magneticDistance;
                vec2 magneticNormal;
                magneticData(local, magneticDistance, magneticNormal);
                float sdf = max(roundedDistance, magneticDistance);
                // FenceWidget is painted above this child.  Draw only the
                // outward lens band; the cached blur/tint and all content stay
                // in the ordinary QWidget layer.
                if (sdf < -0.5 || sdf > u_edgeBand) {
                    gl_FragColor = vec4(0.0);
                    return;
                }

                float outsideDistance = max(0.0, sdf);
                float rim = 1.0 - smoothstep(0.0, u_rimBand,
                                              outsideDistance);
                float shoulder = smoothstep(0.0, u_rimBand, outsideDistance)
                               * (1.0 - smoothstep(u_rimBand,
                                                   u_edgeBand,
                                                   outsideDistance));
                float edgeVisual = max(rim, shoulder * 0.58);

                vec2 normal = magneticDistance > roundedDistance
                    ? magneticNormal : roundedNormal(local);
                float velocity = clamp(length(u_velocity) / 1800.0, 0.0, 1.0);

                // Strong outward rim plus shallow reverse shoulder.
                float lensProfile = rim * rim - shoulder * 0.18;
                float displacement = u_lensStrength * lensProfile
                                   * (1.0 + velocity * 0.42);
                vec2 offset = normal * displacement
                            / max(u_wallpaperSize, vec2(1.0));
                offset.y = -offset.y;

                // One wallpaper fetch per fragment.  The earlier RGB split
                // tripled texture bandwidth for a barely visible fringe on
                // FTG340; warm/cool edge lighting below preserves material
                // separation without the extra samples.
                vec3 refracted = texture2D(
                    u_wallpaper,
                    clamp(baseUv + offset, 0.0, 1.0)).rgb;

                vec2 cursorDelta = local - u_cursor;
                float cursorGlow = exp(-dot(cursorDelta, cursorDelta) / 11500.0);
                vec2 lightDir = normalize(
                    vec2(-0.55, -0.84)
                    + clamp(u_velocity / 2400.0,
                            vec2(-0.28), vec2(0.28)));
                float facingLight = max(0.0, dot(-normal, lightDir));
                float facingAway = max(0.0, dot(normal, lightDir));
                float specular = pow(facingLight, 5.0)
                               * (0.22 + 0.34 * rim);
                specular += cursorGlow * edgeVisual * 0.20;
                vec3 warmCaustic = vec3(1.0, 0.965, 0.91)
                                 * facingLight * rim * 0.08;
                vec3 coolRim = vec3(0.88, 0.95, 1.0)
                             * facingAway * rim * 0.045;
                vec3 finalColor = refracted
                                + (vec3(specular) + warmCaustic + coolRim)
                                  * u_specularGain;
                float edgeAlpha = edgeVisual * (0.34 + 0.38 * rim);
                float alpha = clamp(edgeAlpha, 0.0, 0.84);
                gl_FragColor = vec4(finalColor, alpha);
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
            initializeGpuQueries();
        }
        fields.insert(QStringLiteral("gpu_timer_supported"),
                      m_gpuTimerSupported);
        fields.insert(QStringLiteral("gpu_timer_enabled"),
                      m_gpuTimerEnabled);
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
        m_frameThrottleClock.restart();
        const qreal dpr = devicePixelRatioF();
        glViewport(0, 0, qRound(width() * dpr), qRound(height() * dpr));
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (!m_shaderReady || !m_canvas || width() <= 0 || height() <= 0)
            return;
        if (!ensureTexture())
            return;

        pollGpuQuery();
        const int gpuSlot = beginGpuQuery();

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
        m_program.setUniformValue(
            "u_rimBand", static_cast<GLfloat>(m_config.rimBandPx));
        m_program.setUniformValue(
            "u_edgeBand", static_cast<GLfloat>(m_config.edgeBandPx));
        m_program.setUniformValue(
            "u_cornerRadius", static_cast<GLfloat>(m_config.cornerRadiusPx));
        m_program.setUniformValue(
            "u_specularGain", static_cast<GLfloat>(m_config.specularGain));
        m_program.enableAttributeArray(m_positionAttribute);
        m_program.setAttributeArray(m_positionAttribute, GL_FLOAT, vertices, 2);

        int drawnFences = 0;
        qreal maxSpeed = 0.0;
        const QPoint cursorCanvas = m_canvas->mapFromGlobal(QCursor::pos());
        for (FenceWidget *fence : m_canvas->m_fences) {
            if (!fence || !fence->isVisible() ||
                fence->width() < 2 || fence->height() < 2 ||
                fence->fenceId() != m_activeFenceId)
                continue;

            const QRect geoCanvas =
                fence->geometry().intersected(m_canvas->rect());
            if (geoCanvas.width() < 2 || geoCanvas.height() < 2)
                continue;

            const int padding = qCeil(m_config.edgeBandPx + 2.0);
            const QRect drawRectCanvas = fence->geometry()
                .adjusted(-padding, -padding, padding, padding)
                .intersected(m_canvas->rect());
            if (drawRectCanvas.width() < 2 || drawRectCanvas.height() < 2)
                continue;
            const QRect geo = geoCanvas.translated(-m_canvasOrigin);
            const QRect drawRect =
                drawRectCanvas.translated(-m_canvasOrigin);

            const FenceMotionState state = m_motion.value(fence->fenceId());
            const qreal speed = state.velocity.length();
            maxSpeed = qMax(maxSpeed, speed);
            const int viewportY = height() - drawRect.y() - drawRect.height();
            glViewport(qRound(drawRect.x() * dpr),
                       qRound(viewportY * dpr),
                       qRound(drawRect.width() * dpr),
                       qRound(drawRect.height() * dpr));
            m_program.setUniformValue(
                "u_widgetSize", QVector2D(fence->width(), fence->height()));
            m_program.setUniformValue(
                "u_widgetOrigin", QVector2D(fence->x(), fence->y()));
            m_program.setUniformValue(
                "u_drawSize", QVector2D(drawRect.width(), drawRect.height()));
            m_program.setUniformValue(
                "u_drawOffset",
                QVector2D(drawRectCanvas.x() - fence->x(),
                          drawRectCanvas.y() - fence->y()));
            m_program.setUniformValue(
                "u_cursor", QVector2D(cursorCanvas.x() - fence->x(),
                                      cursorCanvas.y() - fence->y()));
            m_program.setUniformValue("u_velocity", state.velocity);
            m_program.setUniformValue(
                "u_lensStrength", static_cast<GLfloat>(state.lensStrength));

            const bool hasMagneticContour =
                fence->m_magneticEdge != FenceWidget::MagneticEdge::None &&
                fence->m_magneticContour.size() >= 2;
            const int sourceContourCount = hasMagneticContour
                ? fence->m_magneticContour.size() : 0;
            const bool verticalContour =
                fence->m_magneticEdge == FenceWidget::MagneticEdge::Left ||
                fence->m_magneticEdge == FenceWidget::MagneticEdge::Right;
            std::array<GLfloat, kMagneticContourSamples> contourCoordinates {};
            for (int i = 0; i < kMagneticContourSamples; ++i) {
                if (!hasMagneticContour)
                    break;
                const qreal sourcePosition = i * (sourceContourCount - 1.0)
                                           / (kMagneticContourSamples - 1.0);
                const int sourceA = qBound(
                    0, qFloor(sourcePosition), sourceContourCount - 1);
                const int sourceB = qMin(sourceA + 1,
                                         sourceContourCount - 1);
                const qreal mixValue = sourcePosition - sourceA;
                const QPointF point =
                    fence->m_magneticContour[sourceA]
                        * (1.0 - mixValue)
                    + fence->m_magneticContour[sourceB] * mixValue;
                contourCoordinates[i] = static_cast<GLfloat>(
                    verticalContour ? point.x() : point.y());
            }
            m_program.setUniformValue(
                "u_magneticEdge",
                hasMagneticContour
                    ? static_cast<int>(fence->m_magneticEdge) : 0);
            m_program.setUniformValue(
                "u_contourCount",
                hasMagneticContour ? kMagneticContourSamples : 0);
            if (hasMagneticContour) {
                const QPointF first = fence->m_magneticContour.first();
                const QPointF last = fence->m_magneticContour.last();
                m_program.setUniformValue(
                    "u_contourAxisMin",
                    static_cast<GLfloat>(verticalContour
                        ? first.y() : first.x()));
                m_program.setUniformValue(
                    "u_contourAxisMax",
                    static_cast<GLfloat>(verticalContour
                        ? last.y() : last.x()));
                m_program.setUniformValueArray(
                    "u_contourCoordinate", contourCoordinates.data(),
                    kMagneticContourSamples, 1);
            }

            if (!hasMagneticContour) {
                // For ordinary rounded rectangles, shade only the four outer
                // strips.  The irregular contour case still needs the full
                // bounding box because concave gaps can lie inside its QRect.
                const QRect strips[] = {
                    QRect(drawRect.left(), drawRect.top(),
                          qMax(0, geo.left() - drawRect.left() + 1),
                          drawRect.height()),
                    QRect(geo.right(), drawRect.top(),
                          qMax(0, drawRect.right() - geo.right() + 1),
                          drawRect.height()),
                    QRect(geo.left(), drawRect.top(), geo.width(),
                          qMax(0, geo.top() - drawRect.top() + 1)),
                    QRect(geo.left(), geo.bottom(), geo.width(),
                          qMax(0, drawRect.bottom() - geo.bottom() + 1))
                };
                glEnable(GL_SCISSOR_TEST);
                for (const QRect &strip : strips) {
                    if (strip.width() <= 0 || strip.height() <= 0)
                        continue;
                    glScissor(qRound(strip.x() * dpr),
                              qRound((height() - strip.y() - strip.height())
                                     * dpr),
                              qRound(strip.width() * dpr),
                              qRound(strip.height() * dpr));
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                }
                glDisable(GL_SCISSOR_TEST);
            } else {
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
            ++drawnFences;
        }
        m_program.disableAttributeArray(m_positionAttribute);
        m_texture->release();
        m_program.release();
        endGpuQuery(gpuSlot);
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

    FenceWidget *activeFence()
    {
        if (!m_canvas)
            return nullptr;

        for (FenceWidget *fence : m_canvas->m_fences) {
            if (fence && fence->isVisible() &&
                fence->fenceId() == m_activeFenceId)
                return fence;
        }

        // Prefer the wallpaper-matched Fence for the first frame. The active
        // Fence changes only when a Fence itself moves/resizes/collapses.
        // Repositioning a QOpenGLWidget for every hover event corrupts sibling
        // backing stores on the FTG340 driver and can also race icon clicks.
        for (FenceWidget *fence : m_canvas->m_fences) {
            if (fence && fence->isVisible() &&
                fence->m_magneticEdge != FenceWidget::MagneticEdge::None) {
                m_activeFenceId = fence->fenceId();
                return fence;
            }
        }
        for (FenceWidget *fence : m_canvas->m_fences) {
            if (fence && fence->isVisible()) {
                m_activeFenceId = fence->fenceId();
                return fence;
            }
        }
        return nullptr;
    }

    QRect overlayBounds()
    {
        FenceWidget *fence = activeFence();
        if (!fence || !m_canvas)
            return QRect();
        const int padding = qCeil(m_config.edgeBandPx + 2.0);
        return fence->geometry()
            .adjusted(-padding, -padding, padding, padding)
            .intersected(m_canvas->rect());
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

    bool updateMotionState()
    {
        if (!m_canvas || !m_motionClock.isValid())
            return false;
        const qint64 elapsedMs = qMax<qint64>(1, m_motionClock.restart());
        const qreal scale = 1000.0 / elapsedMs;
        QHash<QString, bool> seen;
        bool active = false;

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
            state.velocity = state.velocity * 0.64f + instantaneous * 0.36f;
            state.lastPos = pos;
            const qreal speed = qMin<qreal>(1.0,
                state.velocity.length() / m_config.velocityNormPxS);
            const qreal target = m_config.baseLensStrengthPx
                               + m_config.velocityBoostPx * speed;
            state.lensStrength =
                state.lensStrength * (1.0 - m_config.response)
                + target * m_config.response;
            if (delta.isNull())
                state.velocity *= 0.82f;
            state.peakVelocity = qMax<qreal>(state.peakVelocity,
                                            state.velocity.length());
            state.peakLensStrength = qMax(state.peakLensStrength,
                                          state.lensStrength);
            if (state.velocity.length() > 2.0 ||
                qAbs(state.lensStrength - m_config.baseLensStrengthPx) > 0.03)
                active = true;
        }

        const auto keys = m_motion.keys();
        for (const QString &key : keys) {
            if (!seen.contains(key))
                m_motion.remove(key);
        }
        return active;
    }

    void initializeGpuQueries()
    {
        if (!m_config.gpuTimer || !context())
            return;
        const int major = context()->format().majorVersion();
        const int minor = context()->format().minorVersion();
        const bool versionSupports = major > 3 || (major == 3 && minor >= 3);
        const bool extensionSupports = context()->hasExtension(
            QByteArrayLiteral("GL_ARB_timer_query"));
        m_gpuTimerSupported = versionSupports || extensionSupports;
        if (!m_gpuTimerSupported)
            return;
        m_extra = context()->extraFunctions();
        if (!m_extra)
            return;
        m_extra->initializeOpenGLFunctions();
        std::array<GLuint, kGpuQueryCount> ids {};
        m_extra->glGenQueries(static_cast<GLsizei>(ids.size()), ids.data());
        for (int i = 0; i < kGpuQueryCount; ++i)
            m_gpuQueries[i].id = ids[i];
        m_gpuTimerEnabled = true;
    }

    void destroyGpuQueries()
    {
        if (!m_gpuTimerEnabled || !m_extra)
            return;
        std::array<GLuint, kGpuQueryCount> ids {};
        for (int i = 0; i < kGpuQueryCount; ++i) {
            ids[i] = m_gpuQueries[i].id;
            m_gpuQueries[i].pending = false;
        }
        m_extra->glDeleteQueries(static_cast<GLsizei>(ids.size()), ids.data());
        m_gpuTimerEnabled = false;
    }

    void pollGpuQuery()
    {
        if (!m_gpuTimerEnabled || !m_extra)
            return;
        for (int i = 0; i < kGpuQueryCount; ++i) {
            const int index = (m_gpuReadCursor + i) % kGpuQueryCount;
            GpuQuerySlot &slot = m_gpuQueries[index];
            if (!slot.pending)
                continue;
            GLuint available = 0;
            m_extra->glGetQueryObjectuiv(
                slot.id, GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available)
                return;
            GLuint elapsedNs = 0;
            m_extra->glGetQueryObjectuiv(slot.id, GL_QUERY_RESULT, &elapsedNs);
            slot.pending = false;
            m_gpuReadCursor = (index + 1) % kGpuQueryCount;
            const qreal elapsedMs = static_cast<qreal>(elapsedNs) / 1000000.0;
            ++m_gpuSampleCount;
            m_gpuMsTotal += elapsedMs;
            m_gpuMsMax = qMax(m_gpuMsMax, elapsedMs);
            return;
        }
    }

    int beginGpuQuery()
    {
        if (!m_gpuTimerEnabled || !m_extra)
            return -1;
        GpuQuerySlot &slot = m_gpuQueries[m_gpuWriteCursor];
        if (slot.pending)
            return -1;
        m_extra->glBeginQuery(GL_TIME_ELAPSED, slot.id);
        return m_gpuWriteCursor;
    }

    void endGpuQuery(int slotIndex)
    {
        if (slotIndex < 0 || !m_gpuTimerEnabled || !m_extra)
            return;
        m_extra->glEndQuery(GL_TIME_ELAPSED);
        m_gpuQueries[slotIndex].pending = true;
        m_gpuWriteCursor = (slotIndex + 1) % kGpuQueryCount;
    }

    void flushMetrics()
    {
        if (context() && context()->isValid()) {
            makeCurrent();
            pollGpuQuery();
            doneCurrent();
        }
        if (m_frameCount <= 0)
            return;
        QJsonObject fields;
        fields.insert(QStringLiteral("frames"), m_frameCount);
        fields.insert(QStringLiteral("avg_submit_ms"),
                      m_submitMsTotal / m_frameCount);
        fields.insert(QStringLiteral("max_submit_ms"), m_submitMsMax);
        fields.insert(QStringLiteral("drawn_fences"), m_lastDrawnFenceCount);
        qreal intervalMaxSpeed = 0.0;
        qreal intervalMaxLensStrength = m_config.baseLensStrengthPx;
        fields.insert(QStringLiteral("wallpaper_width"), m_textureSize.width());
        fields.insert(QStringLiteral("wallpaper_height"),
                      m_textureSize.height());
        fields.insert(QStringLiteral("timer_active"),
                      m_animationTimer.isActive());
        if (m_gpuSampleCount > 0) {
            fields.insert(QStringLiteral("gpu_samples"), m_gpuSampleCount);
            fields.insert(QStringLiteral("avg_gpu_ms"),
                          m_gpuMsTotal / m_gpuSampleCount);
            fields.insert(QStringLiteral("max_gpu_ms"), m_gpuMsMax);
        }

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
        m_gpuSampleCount = 0;
        m_gpuMsTotal = 0.0;
        m_gpuMsMax = 0.0;
    }

    QPointer<DesktopCanvas> m_canvas;
    const DemoConfig &m_config;
    QOpenGLShaderProgram m_program;
    std::unique_ptr<QOpenGLTexture> m_texture;
    QTimer m_animationTimer;
    QTimer m_metricsTimer;
    QElapsedTimer m_motionClock;
    QElapsedTimer m_frameThrottleClock;
    QHash<QString, FenceMotionState> m_motion;
    QSize m_textureSize;
    QPoint m_canvasOrigin;
    QString m_activeFenceId;
    qint64 m_textureSourceKey = 0;
    int m_positionAttribute = -1;
    int m_frameCount = 0;
    int m_lastDrawnFenceCount = 0;
    qreal m_submitMsTotal = 0.0;
    qreal m_submitMsMax = 0.0;
    qreal m_lastMaxSpeed = 0.0;
    bool m_shaderReady = false;
    bool m_reportedMissingWallpaper = false;
    QOpenGLExtraFunctions *m_extra = nullptr;
    std::array<GpuQuerySlot, kGpuQueryCount> m_gpuQueries {};
    int m_gpuWriteCursor = 0;
    int m_gpuReadCursor = 0;
    int m_gpuSampleCount = 0;
    qreal m_gpuMsTotal = 0.0;
    qreal m_gpuMsMax = 0.0;
    bool m_gpuTimerSupported = false;
    bool m_gpuTimerEnabled = false;
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
        if (!watched || !event)
            return QObject::eventFilter(watched, event);

        if (auto *canvas = qobject_cast<DesktopCanvas *>(watched)) {
            switch (event->type()) {
            case QEvent::Show:
            case QEvent::Polish:
                scheduleOverlay(canvas);
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
        }

        if (auto *fence = qobject_cast<FenceWidget *>(watched);
            fence &&
            (event->type() == QEvent::Move ||
             event->type() == QEvent::Resize ||
             event->type() == QEvent::Show ||
             event->type() == QEvent::Hide)) {
            if (DesktopCanvas *canvas = canvasForObject(watched)) {
                if (auto *overlay = overlayFor(canvas)) {
                    if (event->type() == QEvent::Hide)
                        overlay->syncToCanvas();
                    else
                        overlay->activateFence(fence);
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void scheduleOverlay(DesktopCanvas *canvas)
    {
        if (!canvas || overlayFor(canvas) || m_pending.contains(canvas))
            return;

        m_pending.insert(canvas);
        // The canvas maps itself near the start of its constructor and loads
        // the wallpaper later.  Deferring creation prevents QOpenGLWidget's
        // first synchronous paint from racing the wallpaper cache.
        QTimer::singleShot(0, canvas, [this, canvas] {
            m_pending.remove(canvas);
            if (canvas->isVisible())
                ensureOverlay(canvas);
        });
    }

    DesktopCanvas *canvasForObject(QObject *object) const
    {
        QObject *current = object;
        while (current) {
            if (auto *canvas = qobject_cast<DesktopCanvas *>(current))
                return canvas;
            current = current->parent();
        }
        return nullptr;
    }

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
            overlay->syncToCanvas();
            return;
        }

        auto *overlay = new DesktopLensingOverlay(canvas);
        m_overlays.insert(canvas, overlay);
        connect(canvas, &QObject::destroyed, this, [this, canvas] {
            m_overlays.remove(canvas);
            m_pending.remove(canvas);
        });
        overlay->syncToCanvas();
    }

    QHash<DesktopCanvas *, QPointer<DesktopLensingOverlay>> m_overlays;
    QSet<DesktopCanvas *> m_pending;
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

    const DemoConfig &config = demoConfig();
    QJsonObject fields;
    fields.insert(QStringLiteral("log_path"), glassLogPath());
    fields.insert(QStringLiteral("active_frame_ms"), config.activeFrameMs);
    fields.insert(QStringLiteral("idle_refresh"),
                  QStringLiteral("event_driven"));
    fields.insert(QStringLiteral("rim_band_px"), config.rimBandPx);
    fields.insert(QStringLiteral("edge_band_px"), config.edgeBandPx);
    fields.insert(QStringLiteral("base_lens_strength_px"),
                  config.baseLensStrengthPx);
    fields.insert(QStringLiteral("velocity_boost_px"),
                  config.velocityBoostPx);
    fields.insert(QStringLiteral("velocity_norm_px_s"),
                  config.velocityNormPxS);
    fields.insert(QStringLiteral("response"), config.response);
    fields.insert(QStringLiteral("render_mode"),
                  QStringLiteral("active_fence_edge_only"));
    fields.insert(QStringLiteral("specular_gain"), config.specularGain);
    fields.insert(QStringLiteral("gpu_timer_requested"), config.gpuTimer);
    writeGlassLog(QStringLiteral("demo_installed"), fields);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(installFenceLensingDemo)
