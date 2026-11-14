// Standard library includes
#include <algorithm>
#include <cmath>
#include <vector>
#include <numbers>
#include <random>
#include <iostream>

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

// Random number generator (a single shared engine for now)
std::mt19937 rng(12345);
std::uniform_real_distribution<float> uniform01(0.0f, 1.0f);

// Returns a random float in [0, 1)
float Random01()
{
  return uniform01(rng);
}

// Returns a random direction uniformly distributed over the unit sphere
glm::vec3 RandomUnitVector()
{
  float z = 1.0f - 2.0f * Random01();                        // uniform in [-1, 1]
  float phi = 2.0f * std::numbers::pi_v<float> * Random01(); // angle around z
  float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return glm::vec3(r * std::cos(phi), r * std::sin(phi), z);
}

// Returns a random direction around the normal n, with more directions
// close to n (cosine-weighted), which matches a diffuse BRDF
glm::vec3 RandomDiffuseDirection(const glm::vec3 &n)
{
  glm::vec3 dir = n + RandomUnitVector();
  if (glm::dot(dir, dir) < 1e-8f) // n and the random vector cancelled out
    return n;
  return glm::normalize(dir);
}

// Traces one light path starting at o in direction v
glm::vec3 TracePath(glm::vec3 o, glm::vec3 v)
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
    v = RandomDiffuseDirection(n);
  }
  return c;
}

// Progressive rendering: running sum of all path samples for each pixel
std::vector<glm::vec3> accum;
size_t num_samples = 0;        // number of samples added to every pixel so far
glm::vec3 last_position(0.0f); // camera state that the current sum belongs to
float last_fov = 0.0f;

void InitializePathTracer()
{

  // allocate the image buffer and clear it (set it to zero)
  if (s_exp > 0)
  {
    image_buffer.resize(pow(2, s_exp) * pow(2, s_exp) * 3);
    std::fill(image_buffer.begin(), image_buffer.end(), 0);
  }
}

void UpdatePathTracer()
{
  size_t s = std::pow(2, s_exp);
  float dx = 1.0f / s;

  // start over when the camera moves or the image size changes
  if (camera.Position() != last_position || camera.FieldOfView() != last_fov ||
      accum.size() != s * s)
  {
    accum.assign(s * s, glm::vec3(0.0f));
    num_samples = 0;
    last_position = camera.Position();
    last_fov = camera.FieldOfView();
  }
  num_samples++;
  if (num_samples % 100 == 0)
    std::cout << num_samples << " samples per pixel" << std::endl;

  for (size_t yi = 0; yi < s; yi++)
  {
    for (size_t xi = 0; xi < s; xi++)
    {
      // ray from the camera through a random point inside this pixel
      glm::vec3 o = camera.Position();
      float px = -0.5f + (xi + Random01()) * dx;
      float py = 0.5f - (yi + Random01()) * dx;
      glm::vec3 v = camera.Ray(px, py);

      // add one more path sample, then average all samples so far
      size_t p = yi * s + xi;
      accum[p] += TracePath(o, v);
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
}
