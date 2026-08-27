#include <QApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>

class ShaderProbe final : public QOpenGLWidget
{
public:
    explicit ShaderProbe(QWidget *parent = nullptr) : QOpenGLWidget(parent)
    {
        setFixedSize(320, 120);
        setWindowTitle(QStringLiteral("ukui-fences OpenGL shader probe"));
    }

protected:
    void initializeGL() override
    {
        static const char *vertexSource = R"(
#ifdef GL_ES
precision mediump float;
#endif
attribute vec2 a_position;
varying vec2 v_texCoord;
void main()
{
    v_texCoord = a_position * 0.5 + 0.5;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

        // This is the compatibility-first four-tap fragment shader proposed
        // for the optional small-area Tier 3 path.
        static const char *fragmentSource = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D u_background;
uniform vec2 u_resolution;
uniform float u_bevelWidth;
uniform float u_cornerRadius;

float sdRoundedBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main()
{
    vec2 pixel = (v_texCoord - 0.5) * u_resolution;
    float d = sdRoundedBox(pixel, u_resolution * 0.5, u_cornerRadius);
    if (d > 0.0)
        discard;

    float edge = clamp(-d / u_bevelWidth, 0.0, 1.0);
    vec2 direction = length(pixel) > 0.001 ? normalize(pixel) : vec2(0.0);
    vec2 offset = (1.0 - edge) * direction * 0.008;
    vec4 color = texture2D(u_background, v_texCoord + offset) * 0.40
               + texture2D(u_background, v_texCoord - offset) * 0.30
               + texture2D(u_background, v_texCoord + offset.yx) * 0.15
               + texture2D(u_background, v_texCoord - offset.yx) * 0.15;
    float fresnel = pow(1.0 - edge, 3.0) * 0.6;
    color.rgb += vec3(fresnel);
    gl_FragColor = color * smoothstep(0.0, -1.0, d);
}
)";

        QTextStream out(stdout);
        QOpenGLContext *ctx = context();
        QOpenGLFunctions *functions = ctx ? ctx->functions() : nullptr;
        const auto glText = [functions](GLenum name) -> QString {
            if (!functions)
                return QStringLiteral("unavailable");
            const GLubyte *value = functions->glGetString(name);
            return value ? QString::fromLatin1(
                               reinterpret_cast<const char *>(value))
                         : QStringLiteral("unavailable");
        };

        out << "context.valid=" << (ctx && ctx->isValid() ? "true" : "false")
            << '\n';
        if (ctx) {
            const QSurfaceFormat format = ctx->format();
            out << "context.version=" << format.majorVersion() << '.'
                << format.minorVersion() << '\n'
                << "context.profile=" << static_cast<int>(format.profile())
                << '\n';
        }
        out << "gl.vendor=" << glText(GL_VENDOR) << '\n'
            << "gl.renderer=" << glText(GL_RENDERER) << '\n'
            << "gl.version=" << glText(GL_VERSION) << '\n'
            << "gl.glsl=" << glText(GL_SHADING_LANGUAGE_VERSION) << '\n';

        QOpenGLShaderProgram program;
        const bool vertexOk =
            program.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                            vertexSource);
        if (!vertexOk)
            out << "vertex.log=" << program.log() << '\n';
        const bool fragmentOk =
            program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                            fragmentSource);
        if (!fragmentOk)
            out << "fragment.log=" << program.log() << '\n';
        const bool linkOk = vertexOk && fragmentOk && program.link();
        if (!linkOk)
            out << "link.log=" << program.log() << '\n';
        out << "shader.vertex=" << (vertexOk ? "pass" : "fail") << '\n'
            << "shader.fragment=" << (fragmentOk ? "pass" : "fail") << '\n'
            << "shader.link=" << (linkOk ? "pass" : "fail") << '\n';
        out.flush();

        QTimer::singleShot(0, qApp, [linkOk] {
            QCoreApplication::exit(linkOk ? 0 : 2);
        });
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTimer::singleShot(10000, &app, [] { QCoreApplication::exit(124); });
    ShaderProbe probe;
    probe.show();
    return app.exec();
}
