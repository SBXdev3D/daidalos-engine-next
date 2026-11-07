// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// The one BSDF of the engine (ADR-0028, ADR-0034): Cook-Torrance with the GGX distribution,
// height-correlated Smith visibility and Schlick's Fresnel, over a Lambert diffuse. The mesh pass
// evaluates it for each light, the path tracer evaluates and samples it.

module Bsdf;

static const float kBsdfPi = 3.14159265;

// The BSDF times the cosine of the light's angle: what a surface sends towards view of the light coming
// from toLight, per unit of that light. All directions point away from the surface and are normalized.
public float3 EvaluateBsdf(float3 normal, float3 view, float3 toLight, float3 diffuseColor, float3 f0, float roughness) {
    const float nDotL = saturate(dot(normal, toLight));
    if (nDotL <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 halfway = normalize(view + toLight);
    const float nDotV = max(dot(normal, view), 0.0001);
    const float nDotH = saturate(dot(normal, halfway));
    const float vDotH = saturate(dot(view, halfway));

    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float d = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    const float distribution = alpha2 / (kBsdfPi * d * d);
    const float visibility =
        0.5 / (nDotL * sqrt(nDotV * nDotV * (1.0 - alpha2) + alpha2) +
               nDotV * sqrt(nDotL * nDotL * (1.0 - alpha2) + alpha2));
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);

    const float3 specular = distribution * visibility * fresnel;
    const float3 diffuse = (1.0 - fresnel) * diffuseColor / kBsdfPi;
    return (diffuse + specular) * nDotL;
}

// A direction chosen by the BSDF, and what a path's throughput is multiplied by when it follows it: the
// BSDF times the cosine, over the probability of choosing that direction. Zero when nothing is reflected.
public struct BsdfSample {
    public float3 direction;
    public float3 weight;
}

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// An orthonormal frame around a unit vector (Duff et al. 2017).
void Frame(float3 n, out float3 t, out float3 b) {
    const float sign = n.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (sign + n.z);
    const float c = n.x * n.y * a;
    t = float3(1.0 + sign * n.x * n.x * a, sign * c, -sign * n.x);
    b = float3(c, sign + n.y * n.y * a, -n.y);
}

// A microfacet normal from the GGX distribution of the normals visible from view, in the frame where the
// surface normal is +Z (Heitz 2018).
float3 SampleVisibleNormal(float3 view, float alpha, float2 u) {
    const float3 stretched = normalize(float3(alpha * view.x, alpha * view.y, view.z));
    const float lengthSquared = stretched.x * stretched.x + stretched.y * stretched.y;
    const float3 t1 = lengthSquared > 0.0 ? float3(-stretched.y, stretched.x, 0.0) * rsqrt(lengthSquared)
                                          : float3(1.0, 0.0, 0.0);
    const float3 t2 = cross(stretched, t1);
    const float r = sqrt(u.x);
    const float phi = 2.0 * kBsdfPi * u.y;
    const float p1 = r * cos(phi);
    const float s = 0.5 * (1.0 + stretched.z);
    const float p2 = (1.0 - s) * sqrt(max(1.0 - p1 * p1, 0.0)) + s * r * sin(phi);
    const float3 normal = p1 * t1 + p2 * t2 + sqrt(max(1.0 - p1 * p1 - p2 * p2, 0.0)) * stretched;
    return normalize(float3(alpha * normal.x, alpha * normal.y, max(normal.z, 0.0)));
}

// The probability density of a direction, per solid angle, for each lobe.
float SpecularPdf(float nDotV, float nDotH, float alpha) {
    const float alpha2 = alpha * alpha;
    const float d = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    const float distribution = alpha2 / (kBsdfPi * d * d);
    const float shadowing = 2.0 * nDotV / (nDotV + sqrt(alpha2 + (1.0 - alpha2) * nDotV * nDotV));
    return shadowing * distribution / (4.0 * nDotV);
}

// Chooses the specular lobe with a probability that follows how much of the light each lobe reflects, so
// that metals rarely sample a diffuse lobe they do not have. u holds three uniform random numbers.
float SpecularProbability(float3 normal, float3 view, float3 diffuseColor, float3 f0) {
    const float nDotV = saturate(dot(normal, view));
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - nDotV, 5.0);
    const float specular = Luminance(fresnel);
    const float diffuse = Luminance(diffuseColor * (1.0 - fresnel));
    return specular + diffuse > 0.0 ? clamp(specular / (specular + diffuse), 0.05, 1.0) : 1.0;
}

public BsdfSample SampleBsdf(float3 normal, float3 view, float3 diffuseColor, float3 f0, float roughness, float3 u) {
    BsdfSample result;
    result.direction = normal;
    result.weight = float3(0.0, 0.0, 0.0);
    const float nDotV = dot(normal, view);
    if (nDotV <= 0.0) {
        return result;
    }
    float3 tangent;
    float3 bitangent;
    Frame(normal, tangent, bitangent);
    const float alpha = roughness * roughness;
    const float specularProbability = SpecularProbability(normal, view, diffuseColor, f0);

    float3 direction;
    if (u.x < specularProbability) {
        const float3 localView = float3(dot(view, tangent), dot(view, bitangent), nDotV);
        const float3 halfway = SampleVisibleNormal(localView, alpha, u.yz);
        const float3 localLight = reflect(-localView, halfway);
        direction = tangent * localLight.x + bitangent * localLight.y + normal * localLight.z;
    } else {
        // Cosine-weighted over the hemisphere.
        const float r = sqrt(u.y);
        const float phi = 2.0 * kBsdfPi * u.z;
        direction = tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(max(1.0 - u.y, 0.0));
    }
    const float nDotL = dot(normal, direction);
    if (nDotL <= 0.0) {
        return result;
    }
    const float3 halfway = normalize(view + direction);
    const float pdf = specularProbability * SpecularPdf(max(nDotV, 0.0001), saturate(dot(normal, halfway)), alpha) +
                      (1.0 - specularProbability) * nDotL / kBsdfPi;
    if (pdf <= 0.0) {
        return result;
    }
    result.direction = direction;
    result.weight = EvaluateBsdf(normal, view, direction, diffuseColor, f0, roughness) / pdf;
    return result;
}
