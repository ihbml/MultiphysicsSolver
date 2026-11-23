#pragma once

#include "film.hpp"

namespace cb {

bool cuda_available();
void cuda_flux(Scheme s,std::span<const FaceTask> in,std::span<Flux> out);

struct SourceTask {
    Material material;
    Cavitation nuclei;
    U state;
    FilmValue film;
    FilmLoad load;
    V wall, support;
    Mat moment;
    double dt, drop_speed, drop_diameter;
};
struct SourceResult {
    U state;
    FilmValue film;
    Ghost ghost;
    Mat renormalization;
    bool full_rank;
    ImpactInfo impact;
    double evaporated, limiter;
};
CB_HD inline SourceResult local_sources(const SourceTask& a) {
    SourceResult r{};
    r.state=phase_change(a.material,a.nuclei,a.state,a.dt);
    r.ghost=complete_boundary(a.support);
    r.full_rank=a.moment.inverse(r.renormalization);
    r.film=a.film;
    r.evaporated=add_load(r.film,a.load,a.wall,998.,.001,a.dt);
    r.impact=bai_gosman(a.drop_speed,a.drop_diameter,998.,.001,.072,true);
    r.limiter=venkat(a.dt,-.13,.042,.0002);
    return r;
}
void cuda_sources(std::span<const SourceTask> in,std::span<SourceResult> out);

} // namespace cb