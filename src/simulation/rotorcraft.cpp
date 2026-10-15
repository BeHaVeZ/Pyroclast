#include "rotorcraft.hpp"

#include "explosion_presets.hpp"
#include "../graphics/particle_layout.hpp"
#include "../graphics/realism_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>

namespace pyroclast
{
namespace
{
constexpr double tick_seconds = 1.0 / 60.0;

double saturate(double x)
{
    return std::clamp(x, 0.0, 1.0);
}

double smoothstep(double edge0, double edge1, double x)
{
    const double t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3 - 2 * t);
}

double softplus(double u)
{
    return u > 0 ? u + std::log1p(std::exp(-u)) : std::log1p(std::exp(u));
}

struct Basis
{
    std::array<double, 6> value;
    std::array<double, 6> first;
    std::array<double, 6> second;
};

Basis quintic_basis(double t)
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;
    const double t5 = t4 * t;
    return {{1 - 10 * t3 + 15 * t4 - 6 * t5, t - 6 * t3 + 8 * t4 - 3 * t5, 0.5 * t2 - 1.5 * t3 + 1.5 * t4 - 0.5 * t5,
                10 * t3 - 15 * t4 + 6 * t5, -4 * t3 + 7 * t4 - 3 * t5, 0.5 * t3 - t4 + 0.5 * t5},
        {-30 * t2 + 60 * t3 - 30 * t4, 1 - 18 * t2 + 32 * t3 - 15 * t4, t - 4.5 * t2 + 6 * t3 - 2.5 * t4, 30 * t2 - 60 * t3 + 30 * t4,
            -12 * t2 + 28 * t3 - 15 * t4, 1.5 * t2 - 4 * t3 + 2.5 * t4},
        {-60 * t + 180 * t2 - 120 * t3, -36 * t + 96 * t2 - 60 * t3, 1 - 9 * t + 18 * t2 - 10 * t3, 60 * t - 180 * t2 + 120 * t3,
            -24 * t + 84 * t2 - 60 * t3, 3 * t - 12 * t2 + 10 * t3}};
}

double wrap_angle(double angle)
{
    angle = std::fmod(angle + rotor::pi, 2 * rotor::pi);
    if (angle < 0) angle += 2 * rotor::pi;
    return angle - rotor::pi;
}

double3 wind_at_hub(const WindSettings& wind, std::uint32_t tick, bool gusts, double height, std::uint32_t mask)
{
    const float3 base = wind_velocity(wind, tick, gusts);
    const float clamped = static_cast<float>(std::max(height, 0.0));
    const bool shear = ((mask >> realism_layout::shear_mask_bit) & 1) != 0;
    const auto profile = shear ? wind_at_height(clamped) : WindProfile{0.35f + 0.65f * std::min(clamped / 15, 1.0f), 0};
    const double c = std::cos(profile.veer);
    const double s = std::sin(profile.veer);
    return {(base.x * c - base.z * s) * profile.strength, base.y * profile.strength, (base.x * s + base.z * c) * profile.strength};
}

double3 required_thrust(double3 acceleration, double3 air)
{
    const double speed = length(air);
    return acceleration * rotor::mass + double3{0, rotor::thrust, 0} + air * (0.5 * rotor::density * rotor::drag_area * speed);
}

double contraction(double z)
{
    return std::clamp(z / std::sqrt(z * z + rotor::radius * rotor::radius), static_cast<double>(rotor_layout::rotor_contraction_floor), 1.0);
}

double contracted_radius(double z)
{
    return rotor::radius / std::sqrt(1 + contraction(z));
}

double wake_radius(double z)
{
    const double x = z - rotor_layout::rotor_far_wake_start * rotor::radius;
    return contracted_radius(z) + rotor_layout::rotor_far_wake_growth * 0.5 * (x + std::sqrt(x * x + rotor::radius * rotor::radius));
}

double free_flow(double inflow, double z)
{
    return rotor::area * inflow * wake_radius(z) / contracted_radius(z);
}

double skew_offset(double z)
{
    const double blend = rotor_layout::rotor_skew_blend * rotor::radius;
    return 0.5 * (z + std::sqrt(z * z + blend * blend));
}

RotorStep make_step(double3 hub, double3 air, double thrust_share, const std::function<double(double, double)>& height_at)
{
    RotorStep step;
    step.listed = true;
    step.hub = hub;
    step.thrust_share = thrust_share;
    const double speed = std::sqrt(air.x * air.x + air.z * air.z);
    const double hover = rotor::hover_inflow * std::sqrt(thrust_share);
    const double speed2 = speed * speed;
    const double inflow = std::sqrt(0.5 * (-speed2 + std::sqrt(speed2 * speed2 + 4 * hover * hover * hover * hover)));
    if (!(inflow > 1e-6)) return step;
    double skew_x = -air.x / (1.5 * inflow);
    double skew_z = -air.z / (1.5 * inflow);
    const double skew = std::sqrt(skew_x * skew_x + skew_z * skew_z);
    if (skew > 4)
    {
        skew_x *= 4 / skew;
        skew_z *= 4 / skew;
    }
    double3 ground{hub.x, height_at(hub.x, hub.z), hub.z};
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        const double offset = skew_offset(hub.y - ground.y);
        ground.x = hub.x + skew_x * offset;
        ground.z = hub.z + skew_z * offset;
        ground.y = height_at(ground.x, ground.z);
    }
    const double height = hub.y - ground.y;
    const double effective_height = std::max(height, 0.5 * rotor::radius);
    const double ratio = rotor::radius / (4 * effective_height);
    step.inflow = inflow * (1 - ratio * ratio / (1 + (speed / inflow) * (speed / inflow)));
    step.skew_x = skew_x;
    step.skew_z = skew_z;
    step.height = height;
    step.ground = ground;
    step.effective_thrust = thrust_share * rotor::thrust * inflow / std::sqrt(speed2 + inflow * inflow);
    step.jet_radius = wake_radius(height);
    const double jet = step.jet_radius * std::sqrt(rotor::pi * step.effective_thrust / rotor::density);
    step.jet_flow = jet - free_flow(step.inflow, height);
    constexpr int samples = 160;
    const double reach = rotor::emission_reach * rotor::radius;
    std::array<double, samples> flux{};
    double peak = 0;
    double total = 0;
    for (int index = 0; index < samples; ++index)
    {
        const double r = (index + 0.5) * reach / samples;
        const auto velocity = rotor_frame_velocity(step, r * r, height - 0.5, 0.5);
        const double u = std::abs(velocity.radial);
        peak = std::max(peak, u);
        const double excess = u * u - rotor::saltation_threshold * rotor::saltation_threshold;
        flux[index] = excess > 0 ? 2 * rotor::pi * r * u * excess * (reach / samples) : 0;
        total += flux[index];
    }
    step.peak_outwash = peak;
    const double turned = saturate((2 * rotor::radius - height) / rotor::radius) * std::exp(-(speed / (0.5 * rotor::hover_inflow)) * (speed / (0.5 * rotor::hover_inflow)));
    step.ring = 2.7 * peak * turned;
    step.raw_emission = total;
    if (total > 0)
    {
        int first = 0;
        while (first < samples && flux[first] <= 0) ++first;
        int last = samples - 1;
        while (last > 0 && flux[last] <= 0) --last;
        const double width = reach / samples;
        step.radii_cm[0] = static_cast<std::uint32_t>(std::lround(first * width * 100));
        step.radii_cm[3] = static_cast<std::uint32_t>(std::lround((last + 1) * width * 100));
        double cumulative = 0;
        int quantile = 1;
        for (int index = 0; index < samples && quantile < 3; ++index)
        {
            const double next = cumulative + flux[index];
            while (quantile < 3 && next >= total * quantile / 3.0)
            {
                const double within = flux[index] > 0 ? (total * quantile / 3.0 - cumulative) / flux[index] : 0;
                step.radii_cm[quantile] = static_cast<std::uint32_t>(std::lround((index + within) * width * 100));
                ++quantile;
            }
            cumulative = next;
        }
    }
    return step;
}
}

double length(double3 a)
{
    return std::sqrt(dot(a, a));
}

double3 normalize(double3 a)
{
    const double size = length(a);
    return size > 0 ? a * (1 / size) : double3{};
}

FlightState flight_state(const FlightPlan& plan, std::uint32_t tick)
{
    FlightState state;
    if (plan.knots.empty()) return state;
    const auto& first = plan.knots.front();
    const auto& last = plan.knots.back();
    if (tick <= first.tick || plan.knots.size() == 1)
    {
        const auto& knot = tick <= first.tick ? first : last;
        return {knot.position, knot.velocity, knot.acceleration, knot.heading};
    }
    if (tick >= last.tick) return {last.position, last.velocity, last.acceleration, last.heading};
    const auto next = std::upper_bound(plan.knots.begin(), plan.knots.end(), tick, [](std::uint32_t value, const FlightKnot& knot) {
        return value < knot.tick;
    });
    const auto& b = *next;
    const auto& a = *(next - 1);
    const double duration = (b.tick - a.tick) * tick_seconds;
    const double t = (tick - a.tick) * tick_seconds / duration;
    const auto basis = quintic_basis(t);
    const auto blend = [&](const std::array<double, 6>& w, double scale) {
        return (a.position * w[0] + a.velocity * (w[1] * duration) + a.acceleration * (w[2] * duration * duration) + b.position * w[3] +
                   b.velocity * (w[4] * duration) + b.acceleration * (w[5] * duration * duration)) *
            scale;
    };
    state.position = blend(basis.value, 1);
    state.velocity = blend(basis.first, 1 / duration);
    state.acceleration = blend(basis.second, 1 / (duration * duration));
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double end = a.heading + wrap_angle(b.heading - a.heading);
    state.heading = (2 * t3 - 3 * t2 + 1) * a.heading + (t3 - 2 * t2 + t) * duration * a.yaw_rate + (-2 * t3 + 3 * t2) * end +
        (t3 - t2) * duration * b.yaw_rate;
    return state;
}

double spin_share(const FlightPlan& plan, std::uint32_t tick)
{
    if (plan.spin_down_tick == never || tick <= plan.spin_down_tick) return 1;
    return 1 - saturate((tick - plan.spin_down_tick) / 1200.0);
}

double collective_share(const FlightPlan& plan, std::uint32_t tick)
{
    if (plan.touchdown_tick == never || tick <= plan.touchdown_tick) return 1;
    const double lowered = 1 - 0.9 * saturate((tick - plan.touchdown_tick) / 240.0);
    const double spin = spin_share(plan, tick);
    return lowered * spin * spin;
}

double rotor_phase(const FlightPlan& plan, std::uint32_t tick, double speed)
{
    double seconds = tick * tick_seconds;
    if (plan.spin_down_tick != never && tick > plan.spin_down_tick)
    {
        const double start = plan.spin_down_tick * tick_seconds;
        const double tau = std::min((tick - plan.spin_down_tick) * tick_seconds, 20.0);
        seconds = start + tau - tau * tau / 40;
    }
    return std::fmod(speed * seconds, 0.5 * rotor::pi);
}

FlightBuilder::FlightBuilder(std::string name, std::uint32_t tick, double3 position, double3 velocity)
{
    plan_.name = std::move(name);
    plan_.list_tick = tick;
    state_.tick = tick;
    state_.position = position;
    state_.velocity = velocity;
    const double horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    state_.heading = horizontal > 1e-6 ? std::atan2(velocity.z, velocity.x) : 0;
    plan_.knots.push_back(state_);
}

FlightBuilder::FlightBuilder(FlightPlan plan) : plan_(std::move(plan))
{
    assert(!plan_.knots.empty());
    state_ = plan_.knots.back();
}

void FlightBuilder::append(std::uint32_t ticks, double3 position, double3 velocity, double3 acceleration)
{
    if (ticks == 0) return;
    FlightKnot knot;
    knot.tick = state_.tick + ticks;
    knot.position = position;
    knot.velocity = velocity;
    knot.acceleration = acceleration;
    const double horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    knot.heading = horizontal > 5 ? state_.heading + wrap_angle(std::atan2(velocity.z, velocity.x) - state_.heading) : state_.heading;
    state_ = knot;
    plan_.knots.push_back(knot);
}

void FlightBuilder::cruise(std::uint32_t ticks)
{
    append(ticks, state_.position + state_.velocity * (ticks * tick_seconds), state_.velocity, {});
}

void FlightBuilder::change_velocity(double3 target)
{
    const double3 change = target - state_.velocity;
    const double magnitude = length(change);
    if (magnitude <= 0) return;
    const double3 direction = change * (1 / magnitude);
    const double ramp = rotor::jerk_ticks * tick_seconds;
    const auto steady_ticks = static_cast<std::uint32_t>(std::max(0.0, std::ceil((magnitude / rotor::acceleration_limit - ramp) * 60 - 1e-9)));
    const double steady = steady_ticks * tick_seconds;
    const double acceleration = magnitude / (ramp + steady);
    const double3 a = direction * acceleration;
    double3 position = state_.position + state_.velocity * ramp + a * (ramp * ramp / 6);
    double3 velocity = state_.velocity + a * (ramp / 2);
    append(rotor::jerk_ticks, position, velocity, a);
    if (steady_ticks != 0)
    {
        position = position + velocity * steady + a * (steady * steady / 2);
        velocity = velocity + a * steady;
        append(steady_ticks, position, velocity, a);
    }
    position = position + velocity * ramp + a * (ramp * ramp / 3);
    append(rotor::jerk_ticks, position, target, {});
}

void FlightBuilder::transit_to(double3 point)
{
    const double3 way = point - state_.position;
    const double distance = length(way);
    if (distance <= 0) return;
    const double seconds = std::ceil(std::max({6.0, std::sqrt(5.774 * distance / rotor::acceleration_limit), 1.875 * distance / rotor::cruise_speed}) - 1e-9);
    if (1.875 * std::hypot(way.x, way.z) / seconds > 5) turn_to(std::atan2(way.z, way.x));
    append(static_cast<std::uint32_t>(seconds) * 60, point, {}, {});
}

void FlightBuilder::turn_to(double heading)
{
    const double change = wrap_angle(heading - state_.heading);
    if (std::abs(change) < 1e-6) return;
    FlightKnot knot = state_;
    knot.tick = state_.tick + static_cast<std::uint32_t>(std::max(60.0, std::ceil(std::abs(change) / (rotor::pi / 6) * 60 - 1e-9)));
    knot.velocity = {};
    knot.acceleration = {};
    knot.heading = state_.heading + change;
    knot.yaw_rate = 0;
    state_ = knot;
    plan_.knots.push_back(knot);
}

void FlightBuilder::hold(std::uint32_t ticks)
{
    append(ticks, state_.position, {}, {});
}

void FlightBuilder::vertical_to(double height, double rate)
{
    const double change = height - state_.position.y;
    const double distance = std::abs(change);
    if (distance <= 0) return;
    const auto steady_ticks = static_cast<std::uint32_t>(std::max(0.0, std::ceil((distance / rate - 2) * 60 - 1e-9)));
    const double speed = distance / (2 + steady_ticks * tick_seconds);
    change_velocity({0, change > 0 ? speed : -speed, 0});
    cruise(steady_ticks);
    change_velocity({});
}

void FlightBuilder::land(const Terrain& terrain, std::uint32_t idle_ticks)
{
    const double ground = terrain.height(static_cast<float>(state_.position.x), static_cast<float>(state_.position.z));
    const double touchdown = ground + rotor::hub_above_skids;
    const double eased = 1.8;
    const double last = 0.3;
    const double start = touchdown + eased + last;
    const double descent = state_.position.y - start - rotor::landing_rate;
    if (descent > 0)
    {
        const auto steady_ticks = static_cast<std::uint32_t>(std::floor(descent / rotor::landing_rate * 60));
        change_velocity({0, -rotor::landing_rate, 0});
        cruise(steady_ticks);
        const double left = state_.position.y - start;
        change_velocity({0, -rotor::touchdown_rate, 0});
        cruise(static_cast<std::uint32_t>(std::lround(left / rotor::touchdown_rate * 60)));
        change_velocity({});
    }
    else vertical_to(touchdown, rotor::landing_rate);
    state_.position.y = touchdown;
    plan_.knots.back().position.y = touchdown;
    plan_.touchdown_tick = state_.tick;
    plan_.spin_down_tick = state_.tick + 240 + idle_ticks;
}

void FlightBuilder::depart(double3 direction, std::uint32_t ticks)
{
    const std::uint32_t start = state_.tick;
    direction.y = 0;
    direction = normalize(direction);
    change_velocity(direction * rotor::cruise_speed + double3{0, rotor::descent_rate, 0});
    if (state_.tick < start + ticks) cruise(start + ticks - state_.tick);
    unlist_at(start + ticks);
}

void FlightBuilder::unlist_at(std::uint32_t tick)
{
    plan_.unlist_tick = tick;
}

FlightPlan FlightBuilder::build() const
{
    return plan_;
}

double helicopter_fade(const FlightPlan& plan, std::uint32_t tick)
{
    if (tick < plan.list_tick || tick >= plan.unlist_tick) return 0;
    const double in = smoothstep(0, 120, static_cast<double>(tick - plan.list_tick));
    const double out = plan.unlist_tick == never ? 1 : smoothstep(0, 120, static_cast<double>(plan.unlist_tick - tick));
    return std::min(in, out);
}

HelicopterPose helicopter_pose(const FlightPlan& plan, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask)
{
    HelicopterPose pose;
    const auto state = flight_state(plan, tick);
    pose.hub = state.position;
    const double ground = terrain.height(static_cast<float>(state.position.x), static_cast<float>(state.position.z));
    pose.skid_height = state.position.y - rotor::hub_above_skids - ground;
    const double3 air = state.velocity - wind_at_hub(wind, tick, gusts, state.position.y - ground, mask);
    double3 up = normalize(required_thrust(state.acceleration, air));
    const double3 heading{std::cos(state.heading), 0, std::sin(state.heading)};
    double3 forward = normalize(heading - up * dot(heading, up));
    const double pitch = 2 * rotor::pi / 180 * (1 - smoothstep(0, 20, length(air)));
    const double3 pitched_forward = forward * std::cos(pitch) + up * std::sin(pitch);
    const double3 pitched_up = up * std::cos(pitch) - forward * std::sin(pitch);
    const double flying = saturate(pose.skid_height / 2);
    up = normalize(double3{0, 1, 0} * (1 - flying) + pitched_up * flying);
    forward = normalize(pitched_forward - up * dot(pitched_forward, up));
    pose.up = up;
    pose.forward = forward;
    pose.right = cross(forward, up);
    pose.spin = spin_share(plan, tick);
    pose.main_phase = rotor_phase(plan, tick, rotor_layout::rotor_speed);
    pose.tail_phase = rotor_phase(plan, tick, rotor_layout::tail_rotor_speed);
    pose.fade = helicopter_fade(plan, tick);
    return pose;
}

RotorStep rotor_step(const FlightPlan& plan, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask, double reference_emission)
{
    RotorStep step;
    if (tick < plan.list_tick || tick >= plan.unlist_tick || ((mask >> realism_layout::rotor_mask_bit) & 1) == 0)
    {
        step.hub = flight_state(plan, tick).position;
        return step;
    }
    const auto state = flight_state(plan, tick);
    const auto height_at = [&terrain](double x, double z) { return static_cast<double>(terrain.height(static_cast<float>(x), static_cast<float>(z))); };
    const double ground = height_at(state.position.x, state.position.z);
    const double3 air = state.velocity - wind_at_hub(wind, tick, gusts, state.position.y - ground, mask);
    const double share = std::min(collective_share(plan, tick) * length(required_thrust(state.acceleration, air)) / rotor::thrust, rotor::largest_thrust_share);
    step = make_step(state.position, air, share, height_at);
    const bool raising = ((mask >> realism_layout::brownout_mask_bit) & 1) != 0 && std::abs(step.ground.x) <= 240 && std::abs(step.ground.z) <= 240 &&
        step.peak_outwash > rotor::saltation_threshold && reference_emission > 0;
    const double height_share = std::clamp((6 * rotor::radius - step.height) / (3 * rotor::radius), 0.0, 1.0);
    step.emission = raising ? static_cast<std::uint32_t>(std::lround(rotor_layout::rotor_emission_one * std::min(1.0, step.raw_emission / reference_emission) * height_share)) : 0;
    if (step.emission == 0) step.radii_cm = {};
    return step;
}

double reference_emission()
{
    const auto flat = [](double, double) { return 0.0; };
    return make_step({0, 2 * rotor::radius, 0}, {}, 1, flat).raw_emission;
}

double rotor_stream(const RotorStep& step, double s, double z, double y_ground)
{
    const double height = step.height;
    const double depth = z > 0 ? std::min(z, height) : z;
    const double radius = wake_radius(depth);
    double flow = free_flow(step.inflow, depth);
    if (z > 0) flow += step.jet_flow * smoothstep(0, height, z);
    const double k = rotor_layout::rotor_fermi;
    const double x = s / (radius * radius);
    const double profile = (x - (softplus(k * (x - 1)) - softplus(-k)) / k) / rotor_layout::rotor_fermi_norm;
    const double jet = step.jet_radius;
    const double thickness = std::sqrt(0.25 * jet * jet + rotor_layout::rotor_wall_growth * rotor_layout::rotor_wall_growth * s);
    const double reference = jet * std::sqrt(0.25 + 4 * rotor_layout::rotor_wall_growth * rotor_layout::rotor_wall_growth);
    const double ground = 1 - std::exp(-y_ground / thickness);
    const double over = thickness / reference - 1;
    const double grown = 1 + 0.5 * (over + std::sqrt(over * over + 0.01));
    const double entrained = 1 + (grown - 1) * std::exp(-y_ground / (rotor_layout::rotor_entrainment_height * jet));
    double stream = -flow / (2 * rotor::pi) * profile * ground * entrained;
    if (step.ring > 0)
    {
        const double centre = rotor_layout::rotor_ring_radius * rotor::radius;
        const double core = rotor_layout::rotor_ring_core * rotor::radius;
        const double dr = std::sqrt(s) - centre;
        const double dy = y_ground - rotor_layout::rotor_ring_height * height;
        stream -= step.ring * (s / (2 * centre)) * std::exp(-(dr * dr + dy * dy) / (core * core)) *
            (1 - std::exp(-y_ground / (rotor_layout::rotor_ring_ground * rotor::radius)));
    }
    return stream;
}

FrameVelocity rotor_frame_velocity(const RotorStep& step, double s, double z, double y_ground)
{
    FrameVelocity velocity;
    if (!(step.inflow > 0)) return velocity;
    constexpr double ds = 0.25;
    constexpr double dy = 0.1;
    const double low = std::max(s - ds, 0.0);
    velocity.vertical = (rotor_stream(step, low + 2 * ds, z, y_ground) - rotor_stream(step, low, z, y_ground)) / ds;
    const double down = std::max(y_ground - dy, 0.0);
    const double span = (y_ground + dy) - down;
    const double gradient = (rotor_stream(step, s, z - dy, y_ground + dy) - rotor_stream(step, s, z + (y_ground - down), down)) / span;
    const double r = std::sqrt(s);
    velocity.radial = r > 1e-3 ? -gradient / r : 0;
    return velocity;
}

double3 rotor_air(const RotorStep& step, double3 point, double terrain_height)
{
    if (!(step.inflow > 0)) return {};
    const double z = step.hub.y - point.y;
    const double y_ground = std::max(point.y - terrain_height, 0.0);
    const double offset = skew_offset(z);
    const double x = point.x - (step.hub.x + step.skew_x * offset);
    const double w = point.z - (step.hub.z + step.skew_z * offset);
    const double s = x * x + w * w;
    const double r = std::sqrt(s);
    const double radius = rotor::radius;
    const double window = smoothstep(-rotor_layout::rotor_window_top_end * radius, -rotor_layout::rotor_window_top_start * radius, z) *
        (1 - smoothstep(rotor_layout::rotor_window_side_start * radius, rotor_layout::rotor_window_side_end * radius, r));
    if (window <= 0) return {};
    const auto frame = rotor_frame_velocity(step, s, z, y_ground);
    double3 air{r > 1e-3 ? frame.radial * x / r : 0, frame.vertical, r > 1e-3 ? frame.radial * w / r : 0};
    air = air * window;
    const double speed = length(air);
    if (speed > rotor_layout::rotor_speed_limit) air = air * (rotor_layout::rotor_speed_limit / speed);
    return air;
}

DustCycle dust_cycle(std::uint32_t seed, std::uint32_t local)
{
    const std::uint32_t id = seed * particle_layout::explosion_particles + local;
    DustCycle cycle;
    cycle.length = rotor_layout::rotor_cycle_min_ticks + particle_hash(id ^ rotor_layout::rotor_salt_cycle) % rotor_layout::rotor_cycle_range_ticks;
    cycle.phase = particle_hash(id ^ rotor_layout::rotor_salt_phase) % cycle.length;
    return cycle;
}

bool dust_born(std::uint32_t seed, std::uint32_t local, std::uint32_t cycle, std::uint32_t emission)
{
    const std::uint32_t id = seed * particle_layout::explosion_particles + local;
    return (particle_hash(id ^ particle_hash(cycle ^ rotor_layout::rotor_salt_birth)) & 1023u) < emission;
}

const RotorHistory::Entry* RotorHistory::find(std::uint32_t tick) const
{
    const auto& entry = entries_[tick % length];
    return entry.tick == tick ? &entry : nullptr;
}

void RotorHistory::store(std::uint32_t tick, const RotorStep& step, float3 drift)
{
    auto& entry = entries_[tick % length];
    entry.tick = tick;
    entry.emission = step.emission;
    entry.outer_cm = step.radii_cm[3];
    entry.anchor = to_float3(step.ground) - drift;
}

void RotorHistory::clear()
{
    entries_.fill({});
}

DustCensus dust_census(const RotorHistory& history, std::uint32_t seed, std::uint32_t start_tick, std::uint32_t end_tick,
    std::uint32_t tick, float3 drift, bool layer_enabled)
{
    DustCensus census;
    if (!layer_enabled || tick >= end_tick) return census;
    double3 point;
    double outer = 0;
    double age = 0;
    for (std::uint32_t local = rotor_layout::rotor_disc_records; local < particle_layout::explosion_particles; ++local)
    {
        const auto cycle = dust_cycle(seed, local);
        const std::uint32_t first = start_tick + 1 + cycle.phase;
        if (tick < first) continue;
        const std::uint32_t index = (tick - first) / cycle.length;
        const std::uint32_t begun = first + index * cycle.length;
        const auto* entry = history.find(begun);
        if (!entry || !dust_born(seed, local, index, entry->emission)) continue;
        ++census.alive;
        point = point + to_double3(entry->anchor + drift);
        outer += entry->outer_cm / 100.0;
        age += (tick - begun) * tick_seconds;
    }
    if (census.alive != 0)
    {
        const double share = 1.0 / census.alive;
        census.mean_point = point * share;
        census.mean_outer = outer * share;
        census.mean_age = age * share;
    }
    return census;
}

void Rotorcraft::fly(FlightPlan plan, std::uint32_t seed)
{
    helicopter_ = Helicopter{std::move(plan), seed, reference_emission()};
    variants_ = true;
    request_pending_ = true;
    history_.clear();
}

void Rotorcraft::replan(FlightPlan plan)
{
    assert(helicopter_);
    helicopter_->plan = std::move(plan);
    request_pending_ = true;
}

void Rotorcraft::clear()
{
    helicopter_.reset();
    variants_ = false;
    request_pending_ = false;
    history_.clear();
}

std::uint32_t Rotorcraft::slot_end() const
{
    if (!helicopter_ || helicopter_->plan.unlist_tick == never) return never;
    return helicopter_->plan.unlist_tick + RotorHistory::length;
}
}
