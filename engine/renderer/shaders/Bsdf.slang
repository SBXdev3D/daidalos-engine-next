// Daidalos Engine Next
// Copyright (C) 2011-2026 Stéphane Baudoux
// SPDX-License-Identifier: MIT OR Apache-2.0

// The one BSDF of the engine (ADR-0028, ADR-0034): Cook-Torrance with the GGX distribution,
// height-correlated Smith visibility and Schlick's Fresnel, over a Lambert diffuse. The lighting
// evaluates it for each light, the path tracer evaluates and samples it.

module Bsdf;

static const float kBsdfPi = 3.14159265;

// The two parts of the BSDF times the cosine: the diffuse part per unit of diffuse color, which the
// denoiser filters without the texture's detail, and the specular part (ADR-0039).
public struct BsdfTerms {
    public float3 diffuse;
    public float3 specular;
}

public BsdfTerms EvaluateBsdfTerms(float3 normal, float3 view, float3 toLight, float3 f0, float roughness) {
    BsdfTerms terms;
    terms.diffuse = float3(0.0, 0.0, 0.0);
    terms.specular = float3(0.0, 0.0, 0.0);
    const float nDotL = saturate(dot(normal, toLight));
    if (nDotL <= 0.0) {
        return terms;
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

    terms.specular = distribution * visibility * fresnel * nDotL;
    terms.diffuse = (1.0 - fresnel) / kBsdfPi * nDotL;
    return terms;
}

// The BSDF times the cosine of the light's angle: what a surface sends towards view of the light coming
// from toLight, per unit of that light. All directions point away from the surface and are normalized.
public float3 EvaluateBsdf(float3 normal, float3 view, float3 toLight, float3 diffuseColor, float3 f0, float roughness) {
    const BsdfTerms terms = EvaluateBsdfTerms(normal, view, toLight, f0, roughness);
    return terms.diffuse * diffuseColor + terms.specular;
}

// The fraction of the light from everywhere that a surface reflects specularly towards view: Karis's
// analytic fit of the split-sum lookup. Specular light is divided by it before denoising, and multiplied
// by it after (ADR-0039).
public float3 SpecularAlbedo(float3 f0, float roughness, float nDotV) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    const float2 scaleBias = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * scaleBias.x + scaleBias.y;
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

// A direction from the specular lobe alone: a microfacet normal drawn from the visible normals of the GGX
// distribution, and view reflected about it. Rays in that distribution, averaged and multiplied by the
// specular albedo, give the specular light (ADR-0042). u holds two uniform random numbers.
public float3 SampleSpecularDirection(float3 normal, float3 view, float roughness, float2 u) {
    float3 tangent;
    float3 bitangent;
    Frame(normal, tangent, bitangent);
    const float3 localView = float3(dot(view, tangent), dot(view, bitangent), max(dot(view, normal), 0.0001));
    const float3 halfway = SampleVisibleNormal(normalize(localView), roughness * roughness, u);
    const float3 localLight = reflect(-normalize(localView), halfway);
    return normalize(tangent * localLight.x + bitangent * localLight.y + normal * localLight.z);
}

// The fraction of light a smooth dielectric reflects, the rest going through, for the cosine of the angle
// to its normal on the side the light comes from and the ratio of the indices of refraction, that side's
// over the other's. 1 past the critical angle: all of it is reflected.
public float FresnelDielectric(float cosine, float eta) {
    const float sine2 = eta * eta * max(1.0 - cosine * cosine, 0.0);
    if (sine2 >= 1.0) {
        return 1.0;
    }
    const float cosT = sqrt(1.0 - sine2);
    const float parallel = (eta * cosine - cosT) / (eta * cosine + cosT);
    const float perpendicular = (cosine - eta * cosT) / (cosine + eta * cosT);
    return 0.5 * (parallel * parallel + perpendicular * perpendicular);
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

// The layers over the base (ADR-0056): a clear coat, a dielectric of index 1.5 with its own roughness, which
// reflects first and dims the base by what it reflects; and a sheen, the Charlie distribution of fibres
// (Estevez and Kulla 2017) with Neubelt's visibility, which adds the rim of light of cloth over the base.
public struct Layers {
    public float coat;          // 0 to 1: how much of the clear coat there is.
    public float coatRoughness; // Perceptual, as the base's.
    public float3 sheen;        // The sheen's color; black for none.
    public float sheenRoughness;
}

public Layers NoLayers() {
    Layers layers;
    layers.coat = 0.0;
    layers.coatRoughness = 0.0;
    layers.sheen = float3(0.0, 0.0, 0.0);
    layers.sheenRoughness = 0.0;
    return layers;
}

public bool HasLayers(Layers layers) {
    return layers.coat > 0.0 || any(layers.sheen > 0.0);
}

// The coat's Fresnel at a cosine: a dielectric of index 1.5, whose f0 is 0.04.
float CoatFresnel(float cosine) {
    return 0.04 + 0.96 * pow(1.0 - saturate(cosine), 5.0);
}

// The fraction of light the sheen reflects towards a direction, at a roughness: a rough fit of the albedo of
// the Charlie sheen, which the base below loses (the glTF extension's scaling, approximated).
float SheenAlbedo(float cosine, float roughness) {
    const float r = clamp(roughness, 0.07, 1.0);
    return saturate(0.3 * (1.0 - r) + 0.15 * r) * (1.0 - 0.5 * saturate(cosine));
}

// The GGX lobe of a dielectric layer alone, times the cosine, for a perceptual roughness.
float CoatLobe(float nDotL, float nDotV, float nDotH, float vDotH, float roughness) {
    const float alpha = max(roughness * roughness, 0.002);
    const float alpha2 = alpha * alpha;
    const float d = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    const float distribution = alpha2 / (kBsdfPi * d * d);
    const float visibility = 0.5 / (nDotL * sqrt(nDotV * nDotV * (1.0 - alpha2) + alpha2) +
                                    nDotV * sqrt(nDotL * nDotL * (1.0 - alpha2) + alpha2));
    return distribution * visibility * CoatFresnel(vDotH) * nDotL;
}

// The Charlie sheen, times the cosine.
float3 SheenLobe(float3 sheen, float nDotL, float nDotV, float nDotH, float roughness) {
    const float alpha = max(roughness * roughness, 0.0049);
    const float sinTheta2 = max(1.0 - nDotH * nDotH, 0.0);
    const float distribution = (2.0 + 1.0 / alpha) * pow(sinTheta2, 0.5 / alpha) / (2.0 * kBsdfPi);
    const float visibility = 1.0 / (4.0 * (nDotL + nDotV - nDotL * nDotV));
    return sheen * distribution * visibility * nDotL;
}

// The BSDF with its layers, times the cosine: the base's terms dimmed by what the layers above reflect, and
// the layers' reflections added to the specular part.
public BsdfTerms EvaluateLayeredTerms(float3 normal, float3 view, float3 toLight, float3 f0, float roughness,
                                      Layers layers) {
    BsdfTerms terms = EvaluateBsdfTerms(normal, view, toLight, f0, roughness);
    if (!HasLayers(layers)) {
        return terms;
    }
    const float nDotL = saturate(dot(normal, toLight));
    if (nDotL <= 0.0) {
        return terms;
    }
    const float3 halfway = normalize(view + toLight);
    const float nDotV = max(dot(normal, view), 0.0001);
    const float nDotH = saturate(dot(normal, halfway));
    const float vDotH = saturate(dot(view, halfway));
    // The sheen sits on the base and dims it; the coat sits on both.
    const float sheenMax = max(layers.sheen.r, max(layers.sheen.g, layers.sheen.b));
    const float sheenScale =
        1.0 - sheenMax * max(SheenAlbedo(nDotV, layers.sheenRoughness), SheenAlbedo(nDotL, layers.sheenRoughness));
    terms.diffuse *= sheenScale;
    terms.specular = terms.specular * sheenScale + SheenLobe(layers.sheen, nDotL, nDotV, nDotH, layers.sheenRoughness);
    const float coatScale = 1.0 - layers.coat * max(CoatFresnel(nDotV), CoatFresnel(nDotL));
    terms.diffuse *= coatScale;
    terms.specular = terms.specular * coatScale + layers.coat * CoatLobe(nDotL, nDotV, nDotH, vDotH, layers.coatRoughness);
    return terms;
}

public float3 EvaluateLayeredBsdf(float3 normal, float3 view, float3 toLight, float3 diffuseColor, float3 f0,
                                  float roughness, Layers layers) {
    const BsdfTerms terms = EvaluateLayeredTerms(normal, view, toLight, f0, roughness, layers);
    return terms.diffuse * diffuseColor + terms.specular;
}

// A direction for the path tracer from the BSDF with its layers: the coat's lobe with about the probability
// of what it reflects, else the base's lobes as SampleBsdf chooses them; the sheen, broad, is reached by the
// diffuse lobe's directions. The weight divides by the density of the whole mixture. u: four uniform numbers.
public BsdfSample SampleLayeredBsdf(float3 normal, float3 view, float3 diffuseColor, float3 f0, float roughness,
                                    Layers layers, float4 u) {
    if (!HasLayers(layers)) {
        return SampleBsdf(normal, view, diffuseColor, f0, roughness, u.xyz);
    }
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
    const float coatProbability = clamp(layers.coat * CoatFresnel(nDotV) * 2.0, 0.0, 0.5);
    const float specularProbability = SpecularProbability(normal, view, diffuseColor, f0);
    const float alpha = roughness * roughness;
    const float coatAlpha = max(layers.coatRoughness * layers.coatRoughness, 0.002);
    const float3 localView = float3(dot(view, tangent), dot(view, bitangent), nDotV);

    float3 direction;
    if (u.w < coatProbability) {
        const float3 halfway = SampleVisibleNormal(localView, coatAlpha, u.yz);
        const float3 localLight = reflect(-localView, halfway);
        direction = tangent * localLight.x + bitangent * localLight.y + normal * localLight.z;
    } else if (u.x < specularProbability) {
        const float3 halfway = SampleVisibleNormal(localView, alpha, u.yz);
        const float3 localLight = reflect(-localView, halfway);
        direction = tangent * localLight.x + bitangent * localLight.y + normal * localLight.z;
    } else {
        const float r = sqrt(u.y);
        const float phi = 2.0 * kBsdfPi * u.z;
        direction = tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(max(1.0 - u.y, 0.0));
    }
    const float nDotL = dot(normal, direction);
    if (nDotL <= 0.0) {
        return result;
    }
    const float nDotH = saturate(dot(normal, normalize(view + direction)));
    const float basePdf = specularProbability * SpecularPdf(max(nDotV, 0.0001), nDotH, alpha) +
                          (1.0 - specularProbability) * nDotL / kBsdfPi;
    const float pdf = coatProbability * SpecularPdf(max(nDotV, 0.0001), nDotH, coatAlpha) +
                      (1.0 - coatProbability) * basePdf;
    if (pdf <= 0.0) {
        return result;
    }
    result.direction = direction;
    result.weight = EvaluateLayeredBsdf(normal, view, direction, diffuseColor, f0, roughness, layers) / pdf;
    return result;
}

// What the layers and the base reflect specularly of the light from everywhere towards view: the specular
// albedo the lighting divides and multiplies the specular light by (ADR-0039).
public float3 LayeredSpecularAlbedo(float3 f0, float roughness, float nDotV, Layers layers) {
    const float3 base = SpecularAlbedo(f0, roughness, nDotV);
    if (!HasLayers(layers)) {
        return base;
    }
    const float sheenMax = max(layers.sheen.r, max(layers.sheen.g, layers.sheen.b));
    const float sheen = SheenAlbedo(nDotV, layers.sheenRoughness);
    const float coat = layers.coat * SpecularAlbedo(float3(0.04, 0.04, 0.04), layers.coatRoughness, nDotV).x;
    return (base * (1.0 - sheenMax * sheen) + layers.sheen * sheen) * (1.0 - layers.coat * CoatFresnel(nDotV)) + coat;
}
