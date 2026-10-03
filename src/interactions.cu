#include "interactions.h"

#include "utilities.h"

#include <thrust/random.h>

__host__ __device__ glm::vec3 calculateRandomDirectionInHemisphere(
    glm::vec3 normal,
    thrust::default_random_engine &rng)
{
    thrust::uniform_real_distribution<float> u01(0, 1);

    float up = sqrt(u01(rng)); // cos(theta)
    float over = sqrt(1 - up * up); // sin(theta)
    float around = u01(rng) * TWO_PI;

    // Find a direction that is not the normal based off of whether or not the
    // normal's components are all equal to sqrt(1/3) or whether or not at
    // least one component is less than sqrt(1/3). Learned this trick from
    // Peter Kutz.

    glm::vec3 directionNotNormal;
    if (abs(normal.x) < SQRT_OF_ONE_THIRD)
    {
        directionNotNormal = glm::vec3(1, 0, 0);
    }
    else if (abs(normal.y) < SQRT_OF_ONE_THIRD)
    {
        directionNotNormal = glm::vec3(0, 1, 0);
    }
    else
    {
        directionNotNormal = glm::vec3(0, 0, 1);
    }

    // Use not-normal direction to generate two perpendicular directions
    glm::vec3 perpendicularDirection1 =
        glm::normalize(glm::cross(normal, directionNotNormal));
    glm::vec3 perpendicularDirection2 =
        glm::normalize(glm::cross(normal, perpendicularDirection1));

    return up * normal
        + cos(around) * over * perpendicularDirection1
        + sin(around) * over * perpendicularDirection2;
}

__host__ __device__ void scatterRay(
    PathSegment& pathSegment,
    glm::vec3 intersect,
    glm::vec3 normal,
    bool outside,
    const Material& m,
    thrust::default_random_engine& rng)
{
    glm::vec3 wi = glm::normalize(pathSegment.ray.direction);
    glm::vec3 n = glm::dot(wi, normal) > 0.0f ? -normal : normal;

    if (m.hasRefractive > 0.0f) {
        thrust::uniform_real_distribution<float> u01(0, 1);
        float ior = m.indexOfRefraction;
        float eta = outside ? (1.0f / ior) : ior;
        glm::vec3 refracted = glm::refract(wi, n, eta);
        bool totalInternalReflection = glm::dot(refracted, refracted) < 1e-8f;

        float cosTheta = outside ? -glm::dot(wi, n) : -glm::dot(refracted, n);
        float r0 = (1.0f - ior) / (1.0f + ior);
        r0 = r0 * r0;
        float fresnel = r0 + (1.0f - r0) * powf(1.0f - cosTheta, 5.0f);

        if (totalInternalReflection || u01(rng) < fresnel) {
            pathSegment.ray.direction = glm::reflect(wi, n);
            pathSegment.ray.origin = intersect + n * 0.001f;
        }
        else {
            pathSegment.ray.direction = refracted;
            pathSegment.ray.origin = intersect - n * 0.001f;
        }
        pathSegment.color *= m.specular.color;
        pathSegment.lastPdf = -1.0f;
    }
    else if (m.hasReflective > 0.0f) {
        pathSegment.ray.direction = glm::reflect(wi, n);
        pathSegment.ray.origin = intersect + n * 0.001f;
        pathSegment.color *= m.specular.color;
        pathSegment.lastPdf = -1.0f;
    }
    else {
        pathSegment.ray.direction = calculateRandomDirectionInHemisphere(n, rng);
        pathSegment.lastPdf = glm::dot(pathSegment.ray.direction, n) / PI;
        pathSegment.ray.origin = intersect + n * 0.001f;
        pathSegment.color *= m.color;
    }
}
