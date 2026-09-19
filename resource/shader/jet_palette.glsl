
vec3 jet_palette(float t) {
	t = clamp(t,0,1);
    // Vibrant Jet-style palette
    // t: 0.0 (blue/violet) → 0.5 (green/yellow) → 1.0 (red/magenta)
    vec3 col = vec3(0.0);

    // Red channel
    col.r = clamp(1.5 - abs(4.0 * t - 3.0), 0.0, 1.0);

    // Green channel
    col.g = clamp(1.5 - abs(4.0 * t - 2.0), 0.0, 1.0);

    // Blue channel
    col.b = clamp(1.5 - abs(4.0 * t - 1.0), 0.0, 1.0);

    // Boost saturation — push away from grey
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(lum), col, 1.35);

    return clamp(col, 0.0, 1.0);
}


