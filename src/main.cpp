/*
        +____________+
        /:\         ,:\
       / : \       , : \
      /  :  \     ,  :  \
     /   :   +-----------+
    +....:../:...+   :  /|
    |\   +./.:...`...+ / |
    | \ ,`/  :   :` ,`/  |
    |  \ /`. :   : ` /`  |
    | , +-----------+  ` |
    |,  |   `+...:,.|...`+
    +...|...,'...+  |   /
     \  |  ,     `  |  /
      \ | ,       ` | /
       \|,         `|/
        +___________+

2-Dimensional Representation Of A 3-Dimensional Cross-Section Of A 4-Dimensional Cube
*/

#include "glad/gl.h"
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_glfw.h"

#include "logger.h"
#include "tiny_obj_loader.h"
#include "ease_functions.hpp"

#include "opengl/Framebuffer.hpp"
#include "opengl/Texture.hpp"
#include "opengl/IndexBuffer.hpp"
#include "opengl/VertexBuffer.hpp"
#include "opengl/Shader.hpp"

#include <chrono>
#include <memory>
#include <thread>
#include <iostream>
#include <stdexcept>

struct Mesh
{
    ogl::VertexBuffer vbo;
    ogl::VertexArray vao;
    unsigned count = 0;
};
template <typename T>
struct VelocityVariable
{
    ease::easeFuncPtr<T> easeFunc = ease::outCirc<T>;
    T velocity = T{0};
    T value = T{0};
    T falloff = T{1};
    glm::vec2 edges{0.0f, 10.0f};

    inline void update(float deltatime)
    {
        value += velocity * deltatime;
        T x = glm::clamp((glm::abs(velocity) - T{edges.x}) / (T{edges.y} - T{edges.x}), T{0}, T{1});
        T curve = easeFunc(x);
        velocity -= velocity * curve * deltatime * falloff;
    }
};
struct Data
{
    GLFWwindow *window = nullptr;
    // seconds
    float deltatime = 0.1;
    glm::dvec2 prevMousePos{0};
    VelocityVariable<glm::vec2> yawPitch;
    VelocityVariable<float> distance{
        .value = 3
    };
    float sensitivity = 500;
};

bool init(GLFWwindow **window);
Mesh load(std::string_view path);
void processInput(Data &data);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

int main(int argc, char **argv)
{
    GLFWwindow *window = nullptr;
    if(!init(&window)) {
        LOG_FATAL("failed to init!");
        return -1;
    }
    assert(window);

    // ===================================

    ogl::Cubemap skybox{"res/textures/qwantani_dawn_puresky_2k.hdr"};
    ogl::ShaderProgram displayShader{"shaders/display"};
    ogl::ShaderProgram skyboxShader{"shaders/skybox"};

    Mesh cube = load("res/models/cube.obj");

    ogl::Cubemap flowCubemap{0}; // dummy argument

    // ===================================

    glm::ivec2 windowDim{-1};
    Data data{};
    data.window = window;
    data.distance.falloff = 10;
    glfwSetWindowUserPointer(data.window, &data);
    glfwSetScrollCallback(data.window, scroll_callback);

    // ===================================

    glDisable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (!glfwWindowShouldClose(window))
    {
        auto start = std::chrono::high_resolution_clock::now();
        glfwGetFramebufferSize(window, &windowDim.x, &windowDim.y);
        processInput(data);


        glm::mat4 viewMat = glm::mat4{1.0f};
        viewMat = glm::translate(
            viewMat,
            glm::vec3{0, 0, -data.distance.value}
        );
        viewMat = glm::rotate(
            viewMat,
            glm::radians(data.yawPitch.value.y),
            glm::vec3{1, 0, 0}
        );
        viewMat = glm::rotate(
            viewMat,
            glm::radians(data.yawPitch.value.x),
            glm::vec3{0, 1, 0}
        );
        glm::mat4 projMat = glm::perspective<float>(glm::radians(45.0f), (float) windowDim.x / windowDim.y, 0.01, 100);

        glViewport(0, 0, windowDim.x, windowDim.y);
        glClearColor(0.1, 0.3, 0.3, 1);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ============
        // draw a cube 
        // ============

        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);

        displayShader.bind();
        
        glUniformMatrix4fv(displayShader.getUniform("u_viewMat"),        1, GL_FALSE, &viewMat[0][0]);
        glUniformMatrix4fv(displayShader.getUniform("u_projectionMat"),  1, GL_FALSE, &projMat[0][0]);
        
        cube.vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, cube.count);

        // ==============
        // draw a skybox 
        // ==============

        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);

        skyboxShader.bind();
        skybox.bind(0);

        glUniformMatrix4fv(skyboxShader.getUniform("u_viewMat"),        1, GL_FALSE, &viewMat[0][0]);
        glUniformMatrix4fv(skyboxShader.getUniform("u_projectionMat"),  1, GL_FALSE, &projMat[0][0]);

        // vertices hard-coded in the shader
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 14);

        // ==========================
        while(GLenum err = glGetError() && err != GL_NO_ERROR) LOG_ERROR("opengl error: %i", err); // just in case callback doesent work
        
        glfwSwapBuffers(window);
        glfwPollEvents();
        data.deltatime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1.0E-6;
    }
    
    glfwDestroyWindow(window);
    glfwTerminate();
}
void APIENTRY debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *msg, const void *objMesh)
{
    if(source == GL_DEBUG_SOURCE_SHADER_COMPILER && (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_OTHER)) return; // handled by ShaderProgram class 

    struct OpenGlError {
        GLuint id;
        std::string source;
        std::string type;
        std::string severity;
        std::string msg;
    } error;
    
    error.id = id;
    error.msg = msg;

    switch (source) {
        case GL_DEBUG_SOURCE_API:
        error.source = "api";
        break;

        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        error.source = "window system";
        break;

        case GL_DEBUG_SOURCE_SHADER_COMPILER:
        error.source = "shader compiler";
        break;

        case GL_DEBUG_SOURCE_THIRD_PARTY:
        error.source = "third party";
        break;

        case GL_DEBUG_SOURCE_APPLICATION:
        error.source = "application";
        break;

        case GL_DEBUG_SOURCE_OTHER:
        error.source = "unknown";
        break;

        default:
        error.source = "unknown";
        break;
    }
    switch (type) {
        case GL_DEBUG_TYPE_ERROR:
        error.type = "error";
        break;

        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        error.type = "deprecated behavior warning";
        break;

        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        error.type = "udefined behavior warning";
        break;

        case GL_DEBUG_TYPE_PORTABILITY:
        error.type = "portability warning";
        break;

        case GL_DEBUG_TYPE_PERFORMANCE:
        error.type = "performance warning";
        break;

        case GL_DEBUG_TYPE_OTHER:
        error.type = "message";
        break;

        case GL_DEBUG_TYPE_MARKER:
        error.type = "marker message";
        break;

        default:
        error.type = "unknown message";
        break;
    }
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
        error.severity = "high";
        break;

        case GL_DEBUG_SEVERITY_MEDIUM:
        error.severity = "medium";
        break;

        case GL_DEBUG_SEVERITY_LOW:
        error.severity = "low";
        break;

        case GL_DEBUG_SEVERITY_NOTIFICATION:
        error.severity = "notification";
        break;

        default:
        error.severity = "unknown";
        break;
    }

    LOG_WARN("%d: opengl %s severity %s, raised from %s:\n\t%s", 
            error.id, 
            error.severity.c_str(), 
            error.type.c_str(), 
            error.source.c_str(), 
            error.msg.c_str());
}
bool init(GLFWwindow **window)
{
    if (!glfwInit()) {
        LOG_FATAL("failed to initialize glfw!");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    GLFWvidmode const *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    *window = glfwCreateWindow(mode->width * 0.7, mode->height * 0.9, "opengl", nullptr, nullptr);
    glfwSetWindowTitle(*window, "flow cubemap editor v1.0");

    if (!*window) {
        LOG_FATAL("failed to initialize window.");
        return false;
    }
    glfwMakeContextCurrent(*window);
    if (!gladLoadGL((GLADloadfunc) glfwGetProcAddress)) {
        LOG_FATAL("gladLoadGL: Failed to initialize GLAD!");
        return false;
    }
    
    ImGui::CreateContext();
    IMGUI_CHECKVERSION();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    if(getenv("WAYLAND_DISPLAY")) 
        LOG_INFO("wayland detected! imgui multiple viewports feature is not supported!");
    else 
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui_ImplGlfw_InitForOpenGL(*window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
    ImGui::StyleColorsDark();
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(debugCallback, nullptr);
    LOG_DEBUG("running in debug mode!");
    
    glfwSwapInterval(0);

    return true;
}
Mesh load(std::string_view path)
{
    tinyobj::ObjReaderConfig config;
    config.mtl_search_path = "./";
    tinyobj::ObjReader reader;

    if(!reader.ParseFromFile(std::string{path}, config)) {
        LOG_ERROR("failed to load \"%s\"!", path.data());
        if(!reader.Error().empty()) {
            LOG_ERROR(reader.Error().c_str());
        }
        return Mesh{
            .count = 0
        };
    }

    if(!reader.Warning().empty()) {
        LOG_WARN(reader.Warning().c_str());
    }

    auto &attrib = reader.GetAttrib();
    auto &shapes = reader.GetShapes();
    auto &materials = reader.GetMaterials();

    std::vector<glm::vec3> positions{};
    std::vector<glm::vec3> normals  {};
    std::vector<glm::vec2> texcoords{};

    Mesh mesh{};
    mesh.count = 0;

    // "unzip" the object by unpacing the indices
    for(auto &shape : shapes) {
        size_t index_offset = 0;
        for(auto &face : shape.mesh.num_face_vertices) {
            for(size_t vertex = 0; vertex < face; ++vertex) {
                // access to vertex
                tinyobj::index_t idx = shape.mesh.indices[index_offset + vertex];
                assert(idx.texcoord_index >= 0);
                assert(idx.normal_index >= 0);

                positions.emplace_back(
                    attrib.vertices[3*size_t(idx.vertex_index)+0],
                    attrib.vertices[3*size_t(idx.vertex_index)+1],
                    attrib.vertices[3*size_t(idx.vertex_index)+2] 
                );
                normals.emplace_back(
                    attrib.normals[3*size_t(idx.normal_index)+0],
                    attrib.normals[3*size_t(idx.normal_index)+1],
                    attrib.normals[3*size_t(idx.normal_index)+2]
                );
                texcoords.emplace_back(
                    attrib.texcoords[2*size_t(idx.texcoord_index)+0],
                    attrib.texcoords[2*size_t(idx.texcoord_index)+1] 
                );

                ++mesh.count;
            }
            index_offset += face;
            // shape.mesh.material_ids[face]; // material
        }
    }

    mesh.vbo = ogl::VertexBuffer{
        positions.size() * sizeof(decltype(positions[0])) +
        normals.size()   * sizeof(decltype(normals[0])) +
        texcoords.size() * sizeof(decltype(texcoords[0]))
    };

    glNamedBufferSubData(mesh.vbo.getRenderID(), 
        0, 
        positions.size() * sizeof(decltype(positions[0])), 
        positions.data()
    );
    glNamedBufferSubData(mesh.vbo.getRenderID(), 
        positions.size() * sizeof(decltype(positions[0])), 
        normals.size()   * sizeof(decltype(normals[0])),
        normals.data()
    );
    glNamedBufferSubData(mesh.vbo.getRenderID(), 
        positions.size() * sizeof(decltype(positions[0])) + normals.size() * sizeof(decltype(normals[0])), 
        texcoords.size() * sizeof(decltype(texcoords[0])),
        texcoords.data()
    );

    ogl::VertexBufferLayout layout = {
        {3, GL_FLOAT, 0},
        {3, GL_FLOAT, positions.size() * sizeof(decltype(positions[0]))},
        {2, GL_FLOAT, positions.size() * sizeof(decltype(positions[0])) + normals.size() * sizeof(decltype(normals[0]))}
    };
    
    mesh.vao = ogl::VertexArray{mesh.vbo, layout};

    return mesh;
}
void processInput(Data &data)
{
    assert(data.window);
    bool cameraLocked = glfwGetMouseButton(data.window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    glfwSetInputMode(data.window, GLFW_CURSOR, cameraLocked ? GLFW_CURSOR_CAPTURED : GLFW_CURSOR_NORMAL);
    glm::dvec2 mousePos{0};
    glfwGetCursorPos(data.window, &mousePos.x, &mousePos.y);
    glm::vec2 deltaMouse = mousePos - data.prevMousePos;
    data.prevMousePos = mousePos;

    if(cameraLocked) 
    {
        data.yawPitch.velocity += deltaMouse * data.deltatime * data.sensitivity;
    }

    data.yawPitch.update(data.deltatime);
    data.yawPitch.falloff = glm::mix(glm::vec2{1.0f}, glm::vec2{5.0f}, static_cast<float>(!cameraLocked));
    data.distance.update(data.deltatime);
    data.distance.value = glm::clamp<float>(data.distance.value, 1, 5);
}
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    Data &data = *static_cast<Data *>(glfwGetWindowUserPointer(window));

    data.distance.velocity -= yoffset * data.deltatime * data.sensitivity;
}
