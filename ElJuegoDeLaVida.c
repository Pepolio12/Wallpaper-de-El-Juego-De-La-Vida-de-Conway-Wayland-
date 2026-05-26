#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <wayland-client.h>
#include <wayland-egl.h> 
#include <EGL/egl.h>
#include <glad/glad.h>

#include "wlr-layer-shell.h"
#include "xdg-shell.h"

#define ANCHO_GRILLA 320
#define ALTO_GRILLA 180

/* --- Estructuras y Variables Globales --- */
struct wl_display *display;
struct wl_compositor *compositor = NULL;
struct zwlr_layer_shell_v1 *layer_shell = NULL;
struct wl_surface *surface;
struct zwlr_layer_surface_v1 *layer_surface;
struct wl_egl_window *egl_window = NULL;

EGLDisplay egl_display;
EGLConfig egl_config;
EGLContext egl_context;
EGLSurface egl_surface;

uint32_t ancho_pantalla = 1280;
uint32_t alto_pantalla = 720;
bool correr_bucle = true;

/* --- Variables de Simulación (OpenGL) --- */
GLuint fbos[2], texturas[2];
GLuint vao, vbo;
GLuint shader_simulacion, shader_render;
int indice_lectura = 0; 

struct timespec ultimo_tiempo = {0, 0};
double intervalo_simulacion = 0.05;

/* Geometría mínima: Quad que cubre toda la pantalla (NDC) */
float quad_vertices[] = {
    -1.0f,  1.0f,  0.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,

    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f
};

/* --- Shaders GLSL ES 3.0 --- */
const char* vs_src = 
    "#version 300 es\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aTexCoords;\n"
    "out vec2 TexCoords;\n"
    "void main() {\n"
    "    TexCoords = aTexCoords;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

/* Kernel de simulación de Conway con bordes toroidales */
const char* fs_sim_src = 
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 TexCoords;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D tex_actual;\n"
    "void main() {\n"
    "    ivec2 coord = ivec2(gl_FragCoord.xy);\n"
    "    int vivos = 0;\n"
    "    for(int i = -1; i <= 1; i++) {\n"
    "        for(int j = -1; j <= 1; j++) {\n"
    "            if(i == 0 && j == 0) continue;\n"
    "            ivec2 vecino_coord = (coord + ivec2(i, j) + ivec2(320, 180)) % ivec2(320, 180);\n"
    "            if(texelFetch(tex_actual, vecino_coord, 0).r > 0.5) vivos++;\n"
    "        }\n"
    "    }\n"
    "    float estado_actual = texelFetch(tex_actual, coord, 0).r;\n"
    "    float proximo_estado = 0.0;\n"
    "    if(estado_actual > 0.5) {\n"
    "        if(vivos == 2 || vivos == 3) proximo_estado = 1.0;\n"
    "    } else {\n"
    "        if(vivos == 3) proximo_estado = 1.0;\n"
    "    }\n"
    "    FragColor = vec4(vec3(proximo_estado), 1.0);\n"
    "}\n";

/* Shader de salida estética (Cyberpunk Green / Charcoal) */
const char* fs_render_src = 
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 TexCoords;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D tex_resultado;\n"
    "void main() {\n"
    "    float celula = texture(tex_resultado, TexCoords).r;\n"
    "    vec4 fondo = vec4(0.05, 0.05, 0.05, 1.0);\n"
    "    vec4 verde = vec4(0.0, 0.8, 0.3, 1.0);\n"
    "    FragColor = mix(fondo, verde, celula);\n"
    "}\n";

/* --- Funciones de Configuración de Pipeline --- */
GLuint compilar_programa(const char* vs, const char* fs) {
    GLuint v_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v_shader, 1, &vs, NULL);
    glCompileShader(v_shader);

    GLuint f_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f_shader, 1, &fs, NULL);
    glCompileShader(f_shader);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, v_shader);
    glAttachShader(prog, f_shader);
    glLinkProgram(prog);

    glDeleteShader(v_shader);
    glDeleteShader(f_shader);
    return prog;
}

void compilar_shaders_y_buffers(void) {
    shader_simulacion = compilar_programa(vs_src, fs_sim_src);
    shader_render = compilar_programa(vs_src, fs_render_src);

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}

void inicializar_simulacion_gpu(void) {
    compilar_shaders_y_buffers(); 

    glGenFramebuffers(2, fbos);
    glGenTextures(2, texturas);

    uint8_t *datos_iniciales = malloc(ANCHO_GRILLA * ALTO_GRILLA * 4);
    srand(time(NULL));

    float centro_x = ANCHO_GRILLA / 2.0f;
    float centro_y = ALTO_GRILLA / 2.0f;
    float radio_maximo = 50.0f; 

    /* Distribución inicial: Concentración de masa celular en el centro */
    for (int y = 0; y < ALTO_GRILLA; y++) {
        for (int x = 0; x < ANCHO_GRILLA; x++) {
            int idx = (y * ANCHO_GRILLA + x) * 4;
            uint8_t val = 0;

            float dx = x - centro_x;
            float dy = y - centro_y;
            float distancia = sqrtf(dx * dx + dy * dy);

            if (distancia < radio_maximo) {
                float factor_cercania = 1.0f - (distancia / radio_maximo);
                if ((rand() % 100) < (factor_cercania * 45)) { 
                    val = 255;
                }
            }

            datos_iniciales[idx]     = val; // R
            datos_iniciales[idx + 1] = val; // G
            datos_iniciales[idx + 2] = val; // B
            datos_iniciales[idx + 3] = 255; // A
        }
    }

    /* Asignar texturas a los FBOs del esquema Ping-Pong */
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, texturas[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ANCHO_GRILLA, ALTO_GRILLA, 0, GL_RGBA, GL_UNSIGNED_BYTE, datos_iniciales);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texturas[i], 0);
    }
    free(datos_iniciales);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
} 

/* --- Lógica Core de Renderizado --- */
static void renderizar_fotograma(void) {
    struct timespec tiempo_actual;
    clock_gettime(CLOCK_MONOTONIC, &tiempo_actual);

    double delta = (tiempo_actual.tv_sec - ultimo_tiempo.tv_sec) + 
                   (tiempo_actual.tv_nsec - ultimo_tiempo.tv_nsec) / 1000000000.0;

    /* Paso 1: Ejecutar iteración del autómata celular (GPGPU Ping-Pong) */
    if (delta >= intervalo_simulacion) {
        int indice_escritura = 1 - indice_lectura;
        
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[indice_escritura]);
        glViewport(0, 0, ANCHO_GRILLA, ALTO_GRILLA);
        
        glUseProgram(shader_simulacion);
        glBindTexture(GL_TEXTURE_2D, texturas[indice_lectura]);
        
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        indice_lectura = indice_escritura; 
        ultimo_tiempo = tiempo_actual;     
    }

    /* Paso 2: Renderizado final escalado a la resolución de pantalla */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ancho_pantalla, alto_pantalla);
    
    glUseProgram(shader_render);
    glBindTexture(GL_TEXTURE_2D, texturas[indice_lectura]);
    
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    eglSwapBuffers(egl_display, egl_surface);
    
    wl_surface_damage_buffer(surface, 0, 0, ancho_pantalla, alto_pantalla);
    wl_surface_commit(surface);
}

/* --- Callbacks e Interfaz de Wayland / Layer Shell --- */
static void handle_frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    (void)data; (void)time;
    wl_callback_destroy(callback);
    
    struct wl_callback *cb = wl_surface_frame(surface);
    static const struct wl_callback_listener frame_listener = { .done = handle_frame_done };
    wl_callback_add_listener(cb, &frame_listener, NULL);
    
    renderizar_fotograma();
}

static void layer_surface_handle_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
                                           uint32_t serial, uint32_t width, uint32_t height) {
    (void)data;
    if (width > 0 && height > 0) {
        ancho_pantalla = width;
        alto_pantalla = height;
    }
    if (egl_window) {
        wl_egl_window_resize(egl_window, ancho_pantalla, alto_pantalla, 0, 0);
    }
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

static void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface) {
    (void)data; (void)layer_surface;
    correr_bucle = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed,
};

static void registry_handle_global(void *data, struct wl_registry *registry, 
                                   uint32_t id, const char *interface, uint32_t version) {
    (void)data; (void)version;
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
        layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};

/* --- Punto de Entrada Principal --- */
int main(void) {
    display = wl_display_connect(NULL);
    if (!display) return -1;

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display); 
    wl_display_roundtrip(display); 

    /* Configuración de la capa del Wallpaper (Background Layer) */
    surface = wl_compositor_create_surface(compositor);
    layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL, 
                                    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "FondoConwayNativo");

    zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener, NULL);
    zwlr_layer_surface_v1_set_anchor(layer_surface, 15); // Expandir a los 4 bordes
    zwlr_layer_surface_v1_set_size(layer_surface, 0, 0);  
    
    wl_surface_commit(surface);
    wl_display_roundtrip(display); 

    /* Inicialización del Contexto EGL para OpenGL ES 3.0 */
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    eglInitialize(egl_display, NULL, NULL);
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint atributos_config[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, 
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint num_config;
    eglChooseConfig(egl_display, atributos_config, &egl_config, 1, &num_config);

    EGLint atributos_contexto[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, atributos_contexto);

    egl_window = wl_egl_window_create(surface, ancho_pantalla, alto_pantalla);
    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)egl_window, NULL);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    /* Carga de punteros OpenGL modernos a través de GLAD */
    gladLoadGLLoader((GLADloadproc)eglGetProcAddress);

    inicializar_simulacion_gpu();

    /* Registro del manejador de frames de Wayland para sincronizar refresco */
    struct wl_callback *cb = wl_surface_frame(surface);
    static const struct wl_callback_listener frame_listener = { .done = handle_frame_done };
    wl_callback_add_listener(cb, &frame_listener, NULL);

    renderizar_fotograma();

    /* Bucle de Eventos Principal */
    while (correr_bucle && wl_display_dispatch(display) != -1) {
        // Manejado de forma asíncrona mediante callbacks de Wayland
    }

    /* Limpieza de Recursos de GPU y Contexto */
    glDeleteFramebuffers(2, fbos);
    glDeleteTextures(2, texturas);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(shader_simulacion);
    glDeleteProgram(shader_render);

    eglDestroySurface(egl_display, egl_surface);
    wl_egl_window_destroy(egl_window);
    zwlr_layer_surface_v1_destroy(layer_surface);
    wl_surface_destroy(surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
    wl_display_disconnect(display);
    
    return 0;
}
