# Parallel Algorithms Ray-Tracing Template
This code provides the infrastructure for a ray-tracing algorithm that we
will be developing as part of this class.

## First Steps (Important)
After pulling this repository, the first thing you should do is make a copy
of the ```pathtrace.cpp.template``` file and rename it to ```pathtrace.cpp```.
For example (on Linux):
```
cp cpp/pathtrace.cpp.template cpp/pathtrace.cpp
```
After doing this, you should be able to build and execute the application.

This will allow you to edit the ```pathtrace.cpp``` file as well as add
additional files yourself. Any updates that I make to this template can easily
be pulled using ```git pull``` without overwriting your changes.

## Initial Template
Once you are able to execute the template, you should see two ImGui windows:
- The ViewportWindow (defined in ```viewport_window.h```) displays an image
buffer directly to the screen. This image is updated every rendering pass
so any changes you make to the buffer will immediately change the image that
is displayed.
- The ControlWindow (defined in ```control_window.h```) provides a series of
controls that you can use to manipulate the camera and change the image
resolution.

There are two functions in ```pathtrace.cpp``` that you will have to modify:
- ```InitializePathTracer()``` is called any time the image resolution is
changed. All this function does right now is re-allocate the buffer and set
all of its values to zero (producing a black image).
- ```UpdatePathTracer()``` is called every render cycle. This
function currently retrieves a vector for each pixel describing a ray direction
and assigns a color to the image buffer associated with the coordinates of
that vector. You can observe this by moving the ```theta``` and ```phi```
controls, which will move the camera in spherical coordinates around the
origin. For example, setting the ```phi``` value to zero will point the camera
so that it is looking directly down the ```z``` axis. Most of the pixels in
the screen should therefore be blue (since most rays are going in the ```z```
direction).

## Phase 1: Build a Path Tracer
The first step of this class project is to implement a basic path tracer.
Feel free to add any additional source files you want to this repository,
but don't add any additional external libraries. I want to provide you with
as much flexibility as possible in your implementation so that you can use
programming techniques that you are comfortable with. For example, you can
stick with traditional C or use object-oriented C++.

### Requirements
There are a few things that we'll need your path tracer to accomplish:
- [ ] You should be able to render a scene with multiple *spheres* defined
by a position $p$ and a radius $r$. You can hard-code them or load them from
some sort of simple text file.
- [ ] The spheres will need some material properties including
`reflectivity` and `emmissivity`. If you have experience with C++ it may be
helpful to encode these properties and intersection methods into a class.
- [ ] Profile the code extensively to optimize the single-threaded performance.
Build a flame graph for intermediate profiling steps and include these in
a brief report describing the bottlenecks you encountered and how you fixed
them.
- [ ] Create a `zip` file from your repository and include it (and your
report) in your submission.

## Phase 2: Parallelize your Path Tracer
Create a parallel implementation of your path tracer using `std::threads`.
I recommend starting with launching a single thread to generate your image,
using the main thread to update the interface. This will allow you to observe
image generation interactively. Then work on parallelizing image rendering
using multiple threads and experiment with different scheduling methods.

### Requirements
- [ ] Implement the image evaluation function in an independent thread so that
you can observe image formation interactively.
- [ ] Implement averaging and acquire multiple samples per pixel to perform
more accurate Monte-Carlo sampling. **Note:** this will require representing
the image using an intermediate floating-point step, since an `unsigned char`
data type is insufficient for averaging.
- [ ] Launch multiple `std::threads` to parallelize rendering. Experiment with
at least 3 different allocation techniques and profile them. Include your
results in a report with flame graphs.
- [ ] Create a `zip` file from your repository and include it (and your report)
in your submission.
