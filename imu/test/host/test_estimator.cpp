// Host unit test: C++ estimator vs. float64 Python reference on identical recordings.
//   make -C test/host test
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "position_estimator.hpp"

using namespace aw;

#ifndef GOLDEN_DIR
#define GOLDEN_DIR "golden"
#endif

static int g_failures = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            ++g_failures;                                 \
            std::printf("  FAIL %s:%d ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

struct Recording {
    std::vector<RawSample> s;
    uint32_t n_prep = 0;
};

static bool load_raw(const std::string& path, Recording* r) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[256];
    if (!std::fgets(line, sizeof line, f)) return false;  // header
    while (std::fgets(line, sizeof line, f)) {
        long long t;
        int phase, a[3], g[3];
        double q[4];
        if (std::sscanf(line, "%lld,%d,%d,%d,%d,%d,%d,%d,%lf,%lf,%lf,%lf", &t, &phase, &a[0], &a[1],
                        &a[2], &g[0], &g[1], &g[2], &q[0], &q[1], &q[2], &q[3]) != 12) {
            continue;
        }
        RawSample x;
        x.t_us = static_cast<uint32_t>(t);
        for (int c = 0; c < 3; ++c) {
            x.acc[c] = static_cast<int16_t>(a[c]);
            x.gyro[c] = static_cast<int16_t>(g[c]);
        }
        r->s.push_back(x);
        if (phase == 0) ++r->n_prep;
    }
    std::fclose(f);
    return !r->s.empty();
}

struct Expected {
    double kp = 0.5, cutoff = 5.0;
    double scalars[8] = {0};
    std::vector<std::vector<double>> all, gesture;
};

static bool load_expected(const std::string& path, Expected* e) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        if (line[0] == 'P') {
            std::sscanf(line + 2, "%lf %lf", &e->kp, &e->cutoff);
        } else if (line[0] == 'S') {
            char key[64];
            double v;
            std::sscanf(line + 2, "%63s %lf", key, &v);
            const char* names[8] = {"flags", "fs_hz", "g_ref", "prep_gyro_rms", "prep_acc_rms",
                                    "end_gyro_rms", "end_acc_rms", "max_dt_error"};
            for (int i = 0; i < 8; ++i) {
                if (!std::strcmp(key, names[i])) e->scalars[i] = v;
            }
        } else if (line[0] == 'A' || line[0] == 'G') {
            std::vector<double> row;
            char* p = line + 2;
            char* end;
            while (true) {
                double v = std::strtod(p, &end);
                if (end == p) break;
                row.push_back(v);
                p = end;
            }
            (line[0] == 'A' ? e->all : e->gesture).push_back(row);
        }
    }
    std::fclose(f);
    return !e->all.empty();
}

struct MaxErr {
    double v = 0;
    void add(double a, double b) {
        const double d = std::fabs(a - b);
        if (d > v) v = d;
    }
};

static void run_case(const char* name) {
    std::printf("case %s\n", name);
    Recording rec;
    Expected exp;
    const std::string base = std::string(GOLDEN_DIR) + "/" + name;
    CHECK(load_raw(base + ".raw.csv", &rec), "cannot read %s.raw.csv", base.c_str());
    CHECK(load_expected(base + ".expected.txt", &exp), "cannot read expected");
    if (rec.s.empty() || exp.all.empty()) return;

    EstimatorParams p;
    p.mahony_kp = static_cast<float>(exp.kp);
    p.lpf_cutoff_hz = static_cast<float>(exp.cutoff);
    static EstimatorResult out;  // 11 KB, keep off the stack
    const EstStatus st = estimate_trajectory(rec.s.data(), rec.s.size(), rec.n_prep, p, &out);
    CHECK(st == EstStatus::kOk, "status %d", static_cast<int>(st));
    if (st != EstStatus::kOk) return;

    CHECK(static_cast<uint32_t>(exp.scalars[0]) == out.flags, "flags C++ %u vs python %u", out.flags,
          static_cast<uint32_t>(exp.scalars[0]));
    MaxErr fs, g, pg, pa, eg, ea;
    fs.add(out.fs_hz, exp.scalars[1]);
    g.add(out.g_ref, exp.scalars[2]);
    pg.add(out.prep_gyro_rms, exp.scalars[3]);
    pa.add(out.prep_acc_rms, exp.scalars[4]);
    eg.add(out.end_gyro_rms, exp.scalars[5]);
    ea.add(out.end_acc_rms, exp.scalars[6]);
    CHECK(fs.v < 1e-3, "fs err %g", fs.v);
    CHECK(g.v < 1e-3, "g_ref err %g", g.v);
    CHECK(pg.v < 1e-4 && pa.v < 1e-3, "prep stats err %g %g", pg.v, pa.v);
    CHECK(eg.v < 1e-3 && ea.v < 1e-3, "end stats err %g %g", eg.v, ea.v);

    MaxErr eq, ew, ef, ev, ep;
    CHECK(exp.all.size() == out.n_total, "n_total");
    for (uint32_t i = 0; i < out.n_total && i < exp.all.size(); ++i) {
        const auto& r = exp.all[i];
        // quaternion sign is not unique; compare after aligning the sign
        const double sgn = (r[1] * out.q[i][0] + r[2] * out.q[i][1] + r[3] * out.q[i][2] +
                            r[4] * out.q[i][3]) < 0 ? -1.0 : 1.0;
        for (int c = 0; c < 4; ++c) eq.add(sgn * out.q[i][c], r[1 + c]);
        for (int c = 0; c < 3; ++c) {
            ew.add(out.acc_world[i][c], r[5 + c]);
            ef.add(out.acc_filt[i][c], r[8 + c]);
        }
    }
    CHECK(exp.gesture.size() == out.n_gesture, "n_gesture");
    for (uint32_t j = 0; j < out.n_gesture && j < exp.gesture.size(); ++j) {
        const auto& r = exp.gesture[j];
        for (int c = 0; c < 3; ++c) {
            ev.add(out.vel[j][c], r[1 + c]);
            ep.add(out.pos[j][c], r[4 + c]);
        }
    }
    std::printf("  max |C++ - python|: quat %.2e  acc_world %.2e m/s2  acc_filt %.2e m/s2  vel %.2e m/s  pos %.2e m\n",
                eq.v, ew.v, ef.v, ev.v, ep.v);
    // float32 on device vs float64 reference
    CHECK(eq.v < 2e-4, "quaternion mismatch");
    CHECK(ew.v < 2e-3, "world acceleration mismatch");
    CHECK(ef.v < 2e-3, "filtered acceleration mismatch");
    CHECK(ev.v < 1e-3, "velocity mismatch");
    CHECK(ep.v < 2e-4, "position mismatch");

    if (!std::strcmp(name, "stationary")) {
        double m = 0;
        for (uint32_t j = 0; j < out.n_gesture; ++j)
            for (int c = 0; c < 3; ++c) m = std::fmax(m, std::fabs(out.pos[j][c]));
        std::printf("  stationary max |pos| %.4f m\n", m);
        CHECK(m < 0.02, "stationary recording drifted by %g m", m);
    }
    if (!std::strcmp(name, "T_not_at_rest")) {
        CHECK(!(out.flags & kFlagDriftCorrected), "drift correction applied to a moving ending");
    } else {
        CHECK(out.flags & kFlagDriftCorrected, "drift correction expected");
        // terminal velocity must be zero after correction
        double v = 0;
        for (int c = 0; c < 3; ++c) v = std::fmax(v, std::fabs(out.vel[out.n_gesture - 1][c]));
        CHECK(v < 1e-4, "terminal velocity %g", v);
    }
}

static void test_filter_design() {
    std::printf("case butterworth\n");
    Biquad b[2];
    CHECK(design_butterworth4(5.0, 30.0, b), "design");
    // unity DC gain per section and |H| = 1/sqrt(2) at the cut-off for the cascade
    for (int i = 0; i < 2; ++i) {
        const double dc = (b[i].b0 + b[i].b1 + b[i].b2) / (1.0 + b[i].a1 + b[i].a2);
        CHECK(std::fabs(dc - 1.0) < 1e-12, "dc gain %g", dc);
    }
    const double w = 2 * 3.14159265358979323846 * 5.0 / 30.0;
    double mag = 1.0;
    for (int i = 0; i < 2; ++i) {
        const double re_n = b[i].b0 + b[i].b1 * std::cos(w) + b[i].b2 * std::cos(2 * w);
        const double im_n = -(b[i].b1 * std::sin(w) + b[i].b2 * std::sin(2 * w));
        const double re_d = 1 + b[i].a1 * std::cos(w) + b[i].a2 * std::cos(2 * w);
        const double im_d = -(b[i].a1 * std::sin(w) + b[i].a2 * std::sin(2 * w));
        mag *= std::sqrt((re_n * re_n + im_n * im_n) / (re_d * re_d + im_d * im_d));
    }
    CHECK(std::fabs(mag - std::sqrt(0.5)) < 1e-9, "cutoff magnitude %g", mag);
    CHECK(!design_butterworth4(15.0, 30.0, b), "fc at Nyquist must be rejected");
    CHECK(!design_butterworth4(0.0, 30.0, b), "fc = 0 must be rejected");
}

static void test_state_reset_and_errors() {
    std::printf("case reset/errors\n");
    Recording rec;
    CHECK(load_raw(std::string(GOLDEN_DIR) + "/H_tilt_wobble.raw.csv", &rec), "load");
    static EstimatorResult a, b;
    EstimatorParams p;
    estimate_trajectory(rec.s.data(), rec.s.size(), rec.n_prep, p, &a);
    // a different recording in between must not influence the next result
    Recording other;
    load_raw(std::string(GOLDEN_DIR) + "/O_tilt_wobble.raw.csv", &other);
    static EstimatorResult mid;
    estimate_trajectory(other.s.data(), other.s.size(), other.n_prep, p, &mid);
    estimate_trajectory(rec.s.data(), rec.s.size(), rec.n_prep, p, &b);
    CHECK(std::memcmp(a.pos, b.pos, sizeof(a.pos)) == 0, "estimator is not stateless");

    // bad inputs
    CHECK(estimate_trajectory(rec.s.data(), 10, 60, p, &a) == EstStatus::kTooFewSamples, "few samples");
    std::vector<RawSample> bad = rec.s;
    bad[100].t_us = bad[99].t_us;
    CHECK(estimate_trajectory(bad.data(), bad.size(), rec.n_prep, p, &a) == EstStatus::kBadTimestamps,
          "timestamps");
    bad = rec.s;
    for (auto& x : bad) x.acc[2] = 0;  // free fall: |a| = 0
    CHECK(estimate_trajectory(bad.data(), bad.size(), rec.n_prep, p, &a) == EstStatus::kBadGravity,
          "gravity");
}

static void test_resample() {
    std::printf("case resample\n");
    Recording rec;
    load_raw(std::string(GOLDEN_DIR) + "/O_tilt_wobble.raw.csv", &rec);
    static EstimatorResult r;
    EstimatorParams p;
    estimate_trajectory(rec.s.data(), rec.s.size(), rec.n_prep, p, &r);
    float out[45][3];
    resample_positions(r.pos, r.tau_s, r.n_gesture, 45, out);
    CHECK(out[0][0] == r.pos[0][0] && std::fabs(out[44][2] - r.pos[89][2]) < 1e-6f, "end points kept");
    // Shape check: the polyline through 45 points must stay within 2 % of the glyph
    // size of the original 90-point polyline.
    double extent = 0, worst = 0;
    for (int c = 0; c < 3; ++c) {
        float lo = 1e9f, hi = -1e9f;
        for (uint32_t j = 0; j < r.n_gesture; ++j) {
            lo = std::fmin(lo, r.pos[j][c]);
            hi = std::fmax(hi, r.pos[j][c]);
        }
        extent = std::fmax(extent, hi - lo);
    }
    // Hausdorff-style check: how far do the original vertices lie from the resampled polyline?
    for (uint32_t k = 0; k < r.n_gesture; ++k) {
        double best = 1e9;
        for (int j = 0; j + 1 < 45; ++j) {
            double d[3], e[3], dd = 0, ee = 0, de = 0;
            for (int c = 0; c < 3; ++c) {
                d[c] = r.pos[k][c] - out[j][c];
                e[c] = out[j + 1][c] - out[j][c];
                dd += d[c] * d[c];
                ee += e[c] * e[c];
                de += d[c] * e[c];
            }
            double t = ee > 0 ? std::fmin(1.0, std::fmax(0.0, de / ee)) : 0.0;
            best = std::fmin(best, std::sqrt(std::fmax(0.0, dd - 2 * t * de + t * t * ee)));
        }
        worst = std::fmax(worst, best);
    }
    std::printf("  45-point resampling: max distance of original points from resampled polyline %.2e m (%.2f %% of glyph)\n",
                worst, 100.0 * worst / extent);
    CHECK(worst / extent < 0.02, "resampling changes the shape");
}

static void test_formats() {
    std::printf("case formats\n");
    Recording rec;
    CHECK(load_raw(std::string(GOLDEN_DIR) + "/H_tilt_wobble.raw.csv", &rec), "load");
    static EstimatorResult r, r2;
    EstimatorParams p;
    estimate_trajectory(rec.s.data(), rec.s.size(), rec.n_prep, p, &r);

    // the `x` request field: x,y,z triples, comma separated, metres
    const std::string x = trajectory_text(r.pos, r.n_gesture);
    size_t commas = 0;
    for (char c : x) commas += (c == ',');
    CHECK(commas == 90 * 3 - 1 && x.find(' ') == std::string::npos && x.find('\n') == std::string::npos, "x layout (%zu commas)", commas);
    char* end = nullptr;
    const double first = std::strtod(x.c_str(), &end);
    CHECK(std::fabs(first - r.pos[0][0]) < 1e-5 && *end == ',', "first value");
    const char* last = std::strrchr(x.c_str(), ',') + 1;
    CHECK(std::fabs(std::strtod(last, nullptr) - r.pos[89][2]) < 1e-5, "last value");

    // the raw CSV is sufficient to reproduce the trajectory exactly
    const std::string csv = raw_csv(rec.s.data(), r);
    mkdir("build", 0755);
    FILE* f = std::fopen("build/_raw.csv", "wb");
    std::fwrite(csv.data(), 1, csv.size(), f);
    std::fclose(f);
    Recording again;
    CHECK(load_raw("build/_raw.csv", &again), "reload");
    estimate_trajectory(again.s.data(), again.s.size(), again.n_prep, p, &r2);
    CHECK(!std::memcmp(r.pos, r2.pos, sizeof r.pos), "positions differ after raw CSV round trip");
    CharInfo ci{0xA1B2C3D4u, 3, 5};
    CHECK(meta_json(ci, 90, p, r).find("\"processing_version\"") != std::string::npos && positions_csv(r).size() > 90 * 20 &&
              format_session_id(0xA1B2C3D4u) == "a1b2c3d4", "metadata / positions / request_id text");
}

int main() {
    const char* cases[] = {"H_tilt_wobble", "O_tilt_wobble", "E_no_gyro_feedback", "W_cutoff_8hz",
                           "stationary",    "T_not_at_rest", "A_jitter"};
    for (const char* c : cases) run_case(c);
    test_filter_design();
    test_state_reset_and_errors();
    test_resample();
    test_formats();
    if (g_failures) {
        std::printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nall estimator checks passed\n");
    return 0;
}
