#include <cstdint>
#include <vector>

struct Particle {
    float x;
    float y;
    float vx;
    float vy;
};

double particle_update(int32_t count, int32_t steps) {
    std::vector<Particle> particles;
    for (int32_t i = 0; i < count; ++i) {
        particles.push_back(Particle{
            float(i & 1023),
            float((i * 3) & 1023),
            0.25F,
            -0.125F,
        });
    }

    for (int32_t step = 0; step < steps; ++step) {
        for (Particle& particle : particles) {
            particle.x += particle.vx;
            particle.y += particle.vy;
        }
    }

    double total = 0.0;
    for (const Particle& particle : particles) {
        total += double(particle.x + particle.y);
    }
    return total;
}
