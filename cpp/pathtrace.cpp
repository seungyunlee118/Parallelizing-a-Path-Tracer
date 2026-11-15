// Standard library includes
#include <algorithm>
#include <cmath>
#include <vector>
#include <numbers>
#include <random>
#include <iostream>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

// Project includes
#include <tira/graphics/camera.h>

extern std::vector<unsigned char> image_buffer;
extern size_t s_exp;

tira::graphics::Camera camera;

// Sphere in the scene
struct Sphere
{
  glm::vec3 p; // center position
  float a;     // radius
  glm::vec3 e; // emissive color: light the sphere gives off
  glm::vec3 d; // diffuse BRDF color
};

// Builds the scene: 8 pastel spheres in a ring, 2 lights above the ring,
// and 1 glowing sphere in the middle (3 lights with different colors)
std::vector<Sphere> MakeScene()
{
  std::vector<Sphere> scene;

  // floor: a huge sphere whose top is at z = -1
  scene.push_back({glm::vec3(0.0f, 0.0f, -101.0f), 100.0f, glm::vec3(0.0f), glm::vec3(0.9f, 0.88f, 0.85f)});

  // 8 pastel diffuse spheres arranged in a ring on the floor
  const glm::vec3 pastel[8] = {
      {0.95f, 0.70f, 0.75f}, // pink
      {0.98f, 0.80f, 0.65f}, // peach
      {0.97f, 0.93f, 0.65f}, // butter yellow
      {0.70f, 0.93f, 0.78f}, // mint
      {0.66f, 0.91f, 0.90f}, // aqua
      {0.68f, 0.84f, 0.95f}, // sky blue
      {0.80f, 0.74f, 0.95f}, // lavender
      {0.93f, 0.72f, 0.90f}, // lilac
  };
  const float ring_radius = 1.8f; // distance from the origin to each sphere
  const float a = 0.4f;           // radius of each sphere
  for (int k = 0; k < 8; k++)
  {
    float angle = 2.0f * std::numbers::pi_v<float> * k / 8.0f;
    glm::vec3 p(ring_radius * std::cos(angle), ring_radius * std::sin(angle), -1.0f + a);
    scene.push_back({p, a, glm::vec3(0.0f), pastel[k]});
  }

  // lights (pastel pink, pastel blue, pastel yellow)
  scene.push_back({glm::vec3(0.0f, 1.0f, 2.0f), 0.5f, glm::vec3(1.0f, 0.35f, 0.6f) * 12.0f, glm::vec3(0.0f)});   // above, pink
  scene.push_back({glm::vec3(0.0f, -1.0f, 2.0f), 0.5f, glm::vec3(0.35f, 0.55f, 1.0f) * 12.0f, glm::vec3(0.0f)}); // above, blue
  scene.push_back({glm::vec3(0.0f, 0.0f, -0.5f), 0.5f, glm::vec3(1.0f, 0.85f, 0.3f) * 5.0f, glm::vec3(0.0f)});   // middle, yellow
  return scene;
}

// All spheres in the scene
std::vector<Sphere> spheres = MakeScene();

float IntersectSphere(const glm::vec3 &o, const glm::vec3 &v, const Sphere &s)
{
  const float eps = 1e-4f;

  glm::vec3 oc = o - s.p;

  float b = glm::dot(v, oc);
  float c = glm::dot(oc, oc) - s.a * s.a;

  float disc = b * b - c;
  if (disc < 0.0f)
    return -1.0f;

  float sq = std::sqrt(disc);
  float t1 = -b - sq;
  float t2 = -b + sq;

  if (t1 > eps)
    return t1;
  if (t2 > eps)
    return t2;
  return -1.0f;
}

// Finds the closest sphere hit by the ray (o + t*v).
// Returns the index of that sphere and stores its distance in t_hit,
// or returns -1 if the ray hits the background.
int ClosestHit(const glm::vec3 &o, const glm::vec3 &v, float &t_hit)
{
  int hit = -1;
  for (size_t i = 0; i < spheres.size(); i++)
  {
    float t = IntersectSphere(o, v, spheres[i]);
    if (t > 0.0f && (hit == -1 || t < t_hit))
    {
      t_hit = t;
      hit = (int)i;
    }
  }
  return hit;
}

// Random numbers: a random engine has internal state that changes on every
// call, so threads must not share one. Every worker thread creates its own
// engine (a local variable in RenderWorker) and passes it down by reference.
// Each engine takes a different seed from rng_seed_counter, so no two engines
// produce the same random sequence.
using Rng = std::mt19937;
std::atomic<unsigned> rng_seed_counter{12345};

// Returns a random float in [0, 1)
float Random01(Rng &rng)
{
  std::uniform_real_distribution<float> uniform01(0.0f, 1.0f);
  return uniform01(rng);
}

// Returns a random direction uniformly distributed over the unit sphere
glm::vec3 RandomUnitVector(Rng &rng)
{
  float z = 1.0f - 2.0f * Random01(rng);                        // uniform in [-1, 1]
  float phi = 2.0f * std::numbers::pi_v<float> * Random01(rng); // angle around z
  float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return glm::vec3(r * std::cos(phi), r * std::sin(phi), z);
}

// Returns a random direction around the normal n, with more directions
// close to n (cosine-weighted), which matches a diffuse BRDF
glm::vec3 RandomDiffuseDirection(const glm::vec3 &n, Rng &rng)
{
  glm::vec3 dir = n + RandomUnitVector(rng);
  if (glm::dot(dir, dir) < 1e-8f) // n and the random vector cancelled out
    return n;
  return glm::normalize(dir);
}

// Traces one light path starting at o in direction v
glm::vec3 TracePath(glm::vec3 o, glm::vec3 v, Rng &rng)
{
  const float r_min = 0.01f;  // stop when the throughput is this small
  const int max_bounces = 10; // safety limit (d = (1,1,1) never reduces r)

  glm::vec3 c(0.0f); // color collected so far
  glm::vec3 r(1.0f); // throughput: fraction of light that still survives

  for (int bounce = 0; bounce < max_bounces; bounce++)
  {
    // find the closest intersection
    float t = 0.0f;
    int i = ClosestHit(o, v, t);
    if (i < 0) // hit the background: nothing more to collect
      break;
    const Sphere &s = spheres[i];

    // add the light emitted by this sphere, scaled by the throughput
    c += r * s.e;

    // reduce the throughput based on the BRDF
    r *= s.d;
    if (std::max({r.x, r.y, r.z}) < r_min)
      break;

    // create a new ray from the hit point in a random BRDF direction
    glm::vec3 x = o + t * v;       // hit point
    glm::vec3 n = (x - s.p) / s.a; // surface normal
    o = x + 1e-4f * n;             // nudge off the surface to avoid self-hits
    v = RandomDiffuseDirection(n, rng);
  }
  return c;
}

// Progressive rendering: running sum of all path samples for each pixel.
// Each pixel is written by exactly one worker thread during a pass, and
// num_samples only changes between passes, so the workers never collide.
std::vector<glm::vec3> accum;
size_t num_samples = 0;        // number of samples added to every pixel so far
glm::vec3 last_position(0.0f); // camera state that the current sum belongs to
float last_fov = 0.0f;

// The main (UI) thread owns `camera` and changes it from the sliders. The render
// thread never reads `camera` directly: the main thread copies it into
// shared_camera, and the render thread takes its own copy at the start of each
// pass. camera_mutex makes sure only one of them touches shared_camera at a time.
std::mutex camera_mutex;
tira::graphics::Camera shared_camera;

std::thread render_thread;               // the thread that runs RenderLoop
std::atomic<bool> stop_rendering{false}; // set by the main thread to stop it

// How the rows of the image are divided between the worker threads
enum class Schedule
{
  Block,  // contiguous: thread t gets the t-th block of s/n neighboring rows
  Cyclic, // round robin: thread t gets rows t, t + n, t + 2n, ...
  Dynamic // work queue: each thread takes the next unrendered row when it is free
};

const char *ScheduleName(Schedule schedule)
{
  switch (schedule)
  {
  case Schedule::Block:
    return "block";
  case Schedule::Cyclic:
    return "cyclic";
  default:
    return "dynamic";
  }
}

// Render settings, read from environment variables in InitializePathTracer:
//   RT_THREADS   number of worker threads (default: number of hardware threads)
//   RT_SCHEDULE  block, cyclic or dynamic (default: dynamic)
//   RT_SPP       samples added to every pixel in each pass (default: 16)
//   RT_BENCH     set to 1 to run the benchmark instead of rendering interactively
int num_threads = 1;
Schedule schedule = Schedule::Dynamic;
int samples_per_pass = 16;
bool run_benchmark = false;

// Returns the value of an environment variable, or "" if it is not set
std::string GetEnv(const char *name)
{
#ifdef _MSC_VER
  // MSVC warns that getenv is unsafe, so use its replacement
  char *value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    return "";
  std::string result(value);
  free(value);
  return result;
#else
  const char *value = std::getenv(name);
  return value ? value : "";
#endif
}

// Adds samples_per_pass new samples to every pixel in row yi, then writes the
// averaged colors of that row into image_buffer
void RenderRow(const tira::graphics::Camera &cam, size_t s, size_t yi, Rng &rng)
{
  float dx = 1.0f / s;
  glm::vec3 o = cam.Position();

  for (size_t xi = 0; xi < s; xi++)
  {
    size_t p = yi * s + xi;

    // new path samples through random points inside this pixel
    for (int k = 0; k < samples_per_pass; k++)
    {
      float px = -0.5f + (xi + Random01(rng)) * dx;
      float py = 0.5f - (yi + Random01(rng)) * dx;
      accum[p] += TracePath(o, cam.Ray(px, py), rng);
    }

    // average all samples so far
    glm::vec3 color = accum[p] / (float)num_samples;

    // if the brightest channel is above what the display can show, scale all
    // three channels down together so the hue survives (instead of turning white)
    float m = std::max({color.r, color.g, color.b});
    if (m > 1.0f)
      color /= m;

    color = glm::pow(color, glm::vec3(1.0f / 2.2f)); // gamma correction for the display

    // convert the [0, 1] color to a [0, 255] pixel value
    size_t idx = p * 3;
    image_buffer[idx + 0] = std::clamp(color.r, 0.0f, 1.0f) * 255;
    image_buffer[idx + 1] = std::clamp(color.g, 0.0f, 1.0f) * 255;
    image_buffer[idx + 2] = std::clamp(color.b, 0.0f, 1.0f) * 255;
  }
}

// The work of worker thread t (out of n) during one pass: renders the rows that
// the schedule gives to this thread. busy_ms receives how long it worked.
void RenderWorker(const tira::graphics::Camera &cam, size_t s, int t, int n,
                  Schedule sched, std::atomic<size_t> &next_row, double &busy_ms)
{
  auto t_start = std::chrono::steady_clock::now();

  // this worker's own random engine (never shared with another thread)
  Rng rng(rng_seed_counter++);

  if (sched == Schedule::Block)
  {
    // rows [first, last) form the t-th of n (almost) equal blocks
    size_t first = s * t / n;
    size_t last = s * (t + 1) / n;
    for (size_t yi = first; yi < last; yi++)
      RenderRow(cam, s, yi, rng);
  }
  else if (sched == Schedule::Cyclic)
  {
    for (size_t yi = t; yi < s; yi += n)
      RenderRow(cam, s, yi, rng);
  }
  else // Schedule::Dynamic
  {
    // fetch_add returns the current value and adds 1 in a single atomic step,
    // so two threads can never take the same row
    for (size_t yi = next_row.fetch_add(1); yi < s; yi = next_row.fetch_add(1))
      RenderRow(cam, s, yi, rng);
  }

  auto t_end = std::chrono::steady_clock::now();
  busy_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
}

// Renders one pass with n worker threads, so that every pixel gets
// samples_per_pass new samples. busy_ms receives each thread's working time.
// Returns when all workers have finished: join() acts as a barrier.
void RenderPass(const tira::graphics::Camera &cam, size_t s, int n, Schedule sched,
                std::vector<double> &busy_ms)
{
  std::atomic<size_t> next_row{0}; // the work queue for Schedule::Dynamic
  busy_ms.assign(n, 0.0);

  // std::thread copies its arguments, so references have to be passed with
  // std::ref / std::cref
  std::vector<std::thread> workers;
  for (int t = 0; t < n; t++)
    workers.emplace_back(RenderWorker, std::cref(cam), s, t, n, sched,
                         std::ref(next_row), std::ref(busy_ms[t]));

  for (std::thread &worker : workers)
    worker.join();
}

// Load imbalance of a pass: the busiest thread's time divided by the average
// time. 1.0 means every thread worked equally long; 2.0 means the slowest
// thread worked twice as long as the average, so the others sat idle.
double Imbalance(const std::vector<double> &busy_ms)
{
  double max_ms = 0.0, sum_ms = 0.0;
  for (double ms : busy_ms)
  {
    max_ms = std::max(max_ms, ms);
    sum_ms += ms;
  }
  return sum_ms > 0.0 ? max_ms / (sum_ms / busy_ms.size()) : 1.0;
}

// Utilization of a pass: the fraction of the available thread time (n threads
// x the pass time) that the threads actually spent rendering. The rest was lost
// to waiting for other threads and to creating / joining the threads.
double Utilization(const std::vector<double> &busy_ms, double pass_ms)
{
  double sum_ms = 0.0;
  for (double ms : busy_ms)
    sum_ms += ms;
  return pass_ms > 0.0 ? sum_ms / (busy_ms.size() * pass_ms) : 1.0;
}

// Average time to create and join n threads that do no work (the fixed cost
// that RenderPass pays on every pass)
double SpawnJoinMs(int n)
{
  const int trials = 200;
  auto t_start = std::chrono::steady_clock::now();
  for (int trial = 0; trial < trials; trial++)
  {
    std::vector<std::thread> workers;
    for (int t = 0; t < n; t++)
      workers.emplace_back([] {});
    for (std::thread &worker : workers)
      worker.join();
  }
  auto t_end = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t_end - t_start).count() / trials;
}

// Measures every schedule with different numbers of threads and prints the
// results as CSV lines. The camera stays where it was at startup.
void RunBenchmark(size_t s)
{
  tira::graphics::Camera cam;
  {
    std::lock_guard<std::mutex> lock(camera_mutex);
    cam = shared_camera;
  }

  const int max_threads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<int> thread_counts;
  for (int n : {1, 2, 4, 8, 12, 16, 20, 24, 32})
    if (n <= max_threads)
      thread_counts.push_back(n);
  if (thread_counts.back() != max_threads)
    thread_counts.push_back(max_threads);

  // Every configuration first renders for warmup_ms without timing, so that
  // idle cores have time to wake up and reach their full clock speed.
  const int repeats = 3;            // run the whole sweep this many times
  const double warmup_ms = 500;     // untimed rendering before timing starts
  const double min_time_ms = 1500;  // time every configuration at least this long
  const int min_timed_passes = 10;  // ... and for at least this many passes

  std::cout << "BENCHMARK: " << s << "x" << s << " pixels, " << samples_per_pass
            << " samples per pixel per pass, " << max_threads << " hardware threads" << std::endl;
  std::cout << "spawn+join overhead per pass (ms):";
  for (int n : thread_counts)
    std::cout << " " << n << "->" << SpawnJoinMs(n);
  std::cout << std::endl;

  std::cout << "repeat,schedule,threads,ms_per_pass,msamples_per_s,imbalance,utilization" << std::endl;

  std::vector<double> busy_ms;
  for (int rep = 1; rep <= repeats; rep++)
  {
    // the schedules alternate inside each thread count, so that slow drifts
    // (for example the laptop heating up) affect all of them equally
    for (int n : thread_counts)
    {
      for (Schedule sched : {Schedule::Block, Schedule::Cyclic, Schedule::Dynamic})
      {
        if (stop_rendering)
          return;

        accum.assign(s * s, glm::vec3(0.0f));
        num_samples = 0;
        auto warmup_start = std::chrono::steady_clock::now();
        while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                         warmup_start).count() < warmup_ms)
        {
          num_samples += samples_per_pass;
          RenderPass(cam, s, n, sched, busy_ms);
        }

        double total_ms = 0.0, imbalance_sum = 0.0, utilization_sum = 0.0;
        int passes = 0;
        while (total_ms < min_time_ms || passes < min_timed_passes)
        {
          num_samples += samples_per_pass;
          auto t_start = std::chrono::steady_clock::now();
          RenderPass(cam, s, n, sched, busy_ms);
          auto t_end = std::chrono::steady_clock::now();
          double pass_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
          total_ms += pass_ms;
          imbalance_sum += Imbalance(busy_ms);
          utilization_sum += Utilization(busy_ms, pass_ms);
          passes++;
        }

        double ms_per_pass = total_ms / passes;
        double msamples_per_s = (s * s * samples_per_pass) / (ms_per_pass * 1000.0);
        std::cout << rep << "," << ScheduleName(sched) << "," << n << "," << ms_per_pass
                  << "," << msamples_per_s << "," << imbalance_sum / passes << ","
                  << utilization_sum / passes << std::endl;
      }
    }
  }
  std::cout << "BENCHMARK DONE" << std::endl;
}

// Body of the render thread: keeps adding passes until it is asked to stop
void RenderLoop()
{
  size_t s = std::pow(2, s_exp);

  if (run_benchmark)
  {
    RunBenchmark(s);
    return;
  }

  std::vector<double> busy_ms;
  double report_ms = 0.0, report_imbalance = 0.0; // totals since the last report
  int report_passes = 0;

  while (!stop_rendering)
  {
    // take a private copy of the latest camera for this whole pass
    tira::graphics::Camera cam;
    {
      std::lock_guard<std::mutex> lock(camera_mutex);
      cam = shared_camera;
    }

    // start over when the camera moves (or on the very first pass)
    if (cam.Position() != last_position || cam.FieldOfView() != last_fov ||
        accum.size() != s * s)
    {
      accum.assign(s * s, glm::vec3(0.0f));
      num_samples = 0;
      last_position = cam.Position();
      last_fov = cam.FieldOfView();
      report_ms = report_imbalance = 0.0;
      report_passes = 0;
    }
    num_samples += samples_per_pass;

    auto t_start = std::chrono::steady_clock::now(); // start the stopwatch
    RenderPass(cam, s, num_threads, schedule, busy_ms);
    auto t_end = std::chrono::steady_clock::now();
    report_ms += std::chrono::duration<double, std::milli>(t_end - t_start).count();
    report_imbalance += Imbalance(busy_ms);
    report_passes++;

    // about once per second of rendering, report the average time per pass
    if (report_ms >= 1000.0)
    {
      double avg_ms = report_ms / report_passes;
      double msamples_per_s = (s * s * samples_per_pass) / (avg_ms * 1000.0);
      std::cout << num_samples << " spp | " << avg_ms << " ms/pass | " << msamples_per_s
                << " M samples/s | imbalance " << report_imbalance / report_passes << std::endl;
      report_ms = report_imbalance = 0.0;
      report_passes = 0;
    }
  }
}

// Asks the render thread to stop and waits until it has finished
void StopRenderThread()
{
  if (render_thread.joinable())
  {
    stop_rendering = true; // the render thread checks this between passes
    render_thread.join();  // wait for it to finish its current pass
  }
}

// Reads the render settings from the environment variables (see above)
void ReadSettings()
{
  num_threads = std::max(1u, std::thread::hardware_concurrency());
  std::string value = GetEnv("RT_THREADS");
  if (!value.empty())
    num_threads = std::max(1, std::atoi(value.c_str()));

  value = GetEnv("RT_SCHEDULE");
  if (value == "block")
    schedule = Schedule::Block;
  else if (value == "cyclic")
    schedule = Schedule::Cyclic;
  else
    schedule = Schedule::Dynamic;

  value = GetEnv("RT_SPP");
  if (!value.empty())
    samples_per_pass = std::max(1, std::atoi(value.c_str()));

  run_benchmark = (GetEnv("RT_BENCH") == "1");

  std::cout << "threads: " << num_threads << ", schedule: " << ScheduleName(schedule)
            << ", samples per pass: " << samples_per_pass
            << (run_benchmark ? ", running the benchmark" : "") << std::endl;
}

void InitializePathTracer()
{
  // the render thread writes to image_buffer, so stop it before resizing
  StopRenderThread();

  // allocate the image buffer and clear it (set it to zero)
  if (s_exp > 0)
  {
    image_buffer.resize(pow(2, s_exp) * pow(2, s_exp) * 3);
    std::fill(image_buffer.begin(), image_buffer.end(), 0);
  }

  // the first time: read the settings, and make sure the render thread is
  // stopped when the program exits (a std::thread that is still running when
  // it is destroyed would crash the program)
  static bool first_call = true;
  if (first_call)
  {
    ReadSettings();
    std::atexit(StopRenderThread);
    first_call = false;
  }

  // give the render thread the current camera, then start it
  {
    std::lock_guard<std::mutex> lock(camera_mutex);
    shared_camera = camera;
  }
  stop_rendering = false;
  render_thread = std::thread(RenderLoop);
}

// Called by the main (UI) thread every frame: passes the latest camera to the
// render thread. The rendering itself happens in RenderLoop.
void UpdatePathTracer()
{
  std::lock_guard<std::mutex> lock(camera_mutex);
  shared_camera = camera;
}
