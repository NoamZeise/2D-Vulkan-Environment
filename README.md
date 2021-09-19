# 2D-Vulkan-Environment
<h5>A 2D renderer for vulkan, ideal for small projects, just add rendering and update code into app.cpp </h5>

features so far:
  
* Handles loading textures and fonts
* Draw textured quads in arbitrary positions
* Draw strings using a loaded font
* Play audio on windows with a filename
* Handle keyboard/mouse input from the user
* Offset texture positions and modify colours




dependancies:

* vulkan sdk (i'm using 1.2.189.1) for included libs, validation layers when debugging, glsl to spirv
* GLFW handles windowing and input
* GLM handles glsl datatypes and matrix operations
* freetype2 for font loading
* stb_image for image loading
* libs:
* Winmm.lib (if on windows, for audio)
* freetyped.lib
* vulkan-1.lib
* glfw3.lib
