# Wayland native Conway's Game of Life (GPGPU)

Un fondo de pantalla dinámico y nativo para entornos Wayland (optimizado para Hyprland) que ejecuta el Juego de la Vida de Conway en paralelo dentro de la GPU mediante *Fragment Shaders* (GPGPU) usando un esquema de renderizado Ping-Pong.

Este repositorio funciona como respaldo de mi configuración y despliegue del wallpaper modular.

## Características & Stack Técnico

- **Lógica en GPU (GPGPU):** La simulación de Conway no sobrecarga la CPU; se calcula píxel por píxel en paralelo usando shaders de fragmentos con condiciones de borde toroidales (efecto bucle en los extremos).
- **Esquema Ping-Pong:** Uso de dos Framebuffer Objects (FBOs) y texturas alternadas (`indice_lectura` / `indice_escritura`) para leer el estado actual y escribir el siguiente fotograma sin pérdida de sincronía.
- **Integración Nativa con Wayland:** Implementado mediante `wlr-layer-shell` acoplándose directamente a la capa de fondo (`ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND`), evitando el uso de wrappers pesados o instancias de XWayland.
- **Contexto Gráfico:** OpenGL ES 3.0 cargado mediante **GLAD** sobre una superficie **EGL** nativa de Wayland.
- **Estética Custom:** Renderizado cosmético con una paleta tonal cyberpunk nocturna (Gris carbón profundo y verde brillante con distribución celular inicial de gradiente central).

## Requisitos de Dependencias

Para compilar este proyecto en Arch Linux, asegurate de contar con las siguientes librerías de desarrollo:
sudo pacman -S wayland wayland-protocols mesa egl-wayland glad libglvnd
