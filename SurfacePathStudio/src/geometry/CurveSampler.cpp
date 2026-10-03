#include "CurveSampler.h"
#include "SurfaceNormal.h"
#include "SurfaceUtils.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Geom2d_Curve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace sps {
namespace {
constexpr double pi = 3.14159265358979323846;
double normalAngle(const gp_Dir& first, const gp_Dir& second) {
    return std::acos(std::clamp(first.Dot(second), -1.0, 1.0));
}
double chordDistance(const gp_Pnt& point, const gp_Pnt& first, const gp_Pnt& last) {
    const gp_Vec chord(first, last);
    if (chord.SquareMagnitude() <= std::numeric_limits<double>::epsilon()) return point.Distance(first);
    const double fraction = std::clamp(gp_Vec(first, point).Dot(chord) / chord.SquareMagnitude(), 0.0, 1.0);
    return point.Distance(first.Translated(chord * fraction));
}
}

std::vector<PathPoint> CurveSampler::sample(const TopoDS_Edge& edge,
                                           const SelectedFace& selected,
                                           const PathParameters& parameters,
                                           CancelCheck cancelled) const {
    checkCancelled(cancelled);
    if (edge.IsNull() || BRep_Tool::Degenerated(edge)) return {};
    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter(), last = curve.LastParameter();
    if (!std::isfinite(first) || !std::isfinite(last) || first >= last)
        throw std::runtime_error("Degenerated or unbounded section curve");
    const double length = GCPnts_AbscissaPoint::Length(curve, first, last, parameters.tolerance.linear);
    if (!std::isfinite(length)) throw std::runtime_error("Cannot compute section curve arc length");
    if (length <= parameters.tolerance.linear) return {};
    const double intervals = std::max(1.0, std::ceil(length / parameters.pointDistance));
    if (intervals + 1 > static_cast<double>(parameters.maxPoints) ||
        intervals + 1 > static_cast<double>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Path point limit exceeded; increase point distance");

    double pcurveFirst, pcurveLast;
    const Handle(Geom2d_Curve) pcurve = BRep_Tool::CurveOnSurface(edge, selected.face, pcurveFirst, pcurveLast);
    const double surfaceTolerance = std::max({parameters.tolerance.linear,
        BRep_Tool::Tolerance(edge), BRep_Tool::Tolerance(selected.face)});
    const auto evaluate = [&](double parameter) {
        checkCancelled(cancelled);
        PathPoint result;
        result.surfacePoint = curve.Value(parameter);
        SurfaceEvaluation surface;
        bool valid = false;
        if (!pcurve.IsNull() && parameter >= pcurveFirst - Precision::PConfusion() &&
            parameter <= pcurveLast + Precision::PConfusion()) {
            const gp_Pnt2d uv = pcurve->Value(parameter);
            valid = evaluateSurfaceUV(selected.face, uv.X(), uv.Y(), parameters.tolerance, surface) &&
                    surface.point.Distance(result.surfacePoint) <= surfaceTolerance;
        }
        if (!valid) valid = evaluateSurfacePoint(selected.face, result.surfacePoint, parameters.tolerance, surface);
        if (!valid) throw std::runtime_error("Surface normal undefined or section point outside trimmed Face");
        result.u = surface.u;
        result.v = surface.v;
        result.surfaceNormal = surface.normal;
        result.faceId = selected.id;
        gp_Pnt derivativePoint;
        gp_Vec tangent;
        try { curve.D1(parameter, derivativePoint, tangent); }
        catch (const Standard_Failure&) { tangent.SetCoord(0, 0, 0); }
        if (tangent.Magnitude() <= Precision::Confusion()) {
            const double delta = std::max((last - first) * 1e-6, Precision::PConfusion());
            const double before = std::max(first, parameter - delta);
            const double after = std::min(last, parameter + delta);
            tangent = gp_Vec(curve.Value(before), curve.Value(after));
        }
        if (tangent.Magnitude() <= Precision::Confusion()) throw std::runtime_error("Degenerated curve tangent");
        result.tangent = gp_Dir(tangent);
        return result;
    };

    std::vector<double> sampleParameters{first};
    if (intervals > 1) {
        // Explicit double selects the arc-length overload, never the point-count
        // or a UV parameter step. A whole interval count keeps end spacing <= request.
        const double distance = length / intervals;
        GCPnts_UniformAbscissa uniform(curve, distance, first, last, parameters.tolerance.linear);
        if (!uniform.IsDone()) throw std::runtime_error("OCCT arc-length sampling failed");
        for (int index = 1; index <= uniform.NbPoints(); ++index) {
            const double parameter = uniform.Parameter(index);
            if (parameter > first && parameter < last)
                sampleParameters.push_back(parameter);
        }
    }
    sampleParameters.push_back(last);
    // Preserve the meaning of an oriented CAD normal even when a caller allows
    // very coarse sampling. Opposite hemispheres need extra samples rather
    // than reinterpreting a correct outward normal as an inward one for continuity.
    constexpr double normalHemisphereLimitDeg = 89.0;
    const double angleLimit = std::min(parameters.normalAngleToleranceDeg, normalHemisphereLimitDeg) * pi / 180.0;
    // Reuse the OCCT curvature/deflection discretizer as an additional seed.
    // It handles curve continuity intervals and high curvature before the
    // surface-normal refinement below. UV spacing is never interpreted as mm.
    GCPnts_TangentialDeflection deflection(curve, first, last,
        angleLimit,
        parameters.chordTolerance, 2, Precision::PConfusion(), parameters.tolerance.linear);
    for (int index = 1; index <= deflection.NbPoints(); ++index) {
        const double parameter = deflection.Parameter(index);
        if (parameter > first && parameter < last) sampleParameters.push_back(parameter);
    }
    std::sort(sampleParameters.begin(), sampleParameters.end());
    sampleParameters.erase(std::unique(sampleParameters.begin(), sampleParameters.end(), [&](double a, double b) {
        return std::abs(a - b) <= (last - first) * std::numeric_limits<double>::epsilon() * 32;
    }), sampleParameters.end());
    if (sampleParameters.size() > parameters.maxPoints)
        throw std::runtime_error("Path point limit exceeded during OCCT deflection sampling");

    const auto halfParameter = [&](double a, double b) {
        const double arcLength = GCPnts_AbscissaPoint::Length(curve, a, b, parameters.tolerance.linear);
        GCPnts_AbscissaPoint half(curve, arcLength * 0.5, a, (a + b) * 0.5, parameters.tolerance.linear);
        if (!half.IsDone() || half.Parameter() <= a || half.Parameter() >= b)
            throw std::runtime_error("OCCT adaptive arc-length subdivision failed");
        return half.Parameter();
    };

    std::vector<PathPoint> result;
    result.reserve(static_cast<std::size_t>(intervals) + 1);
    result.push_back(evaluate(first));
    std::function<void(double, const PathPoint&, double, const PathPoint&, int)> refine;
    refine = [&](double a, const PathPoint& pa, double b, const PathPoint& pb, int depth) {
        checkCancelled(cancelled);
        const double mid = halfParameter(a, b);
        const double quarter = halfParameter(a, mid);
        const double threeQuarter = halfParameter(mid, b);
        const PathPoint pq = evaluate(quarter), pm = evaluate(mid), pr = evaluate(threeQuarter);
        const double chordError = std::max({
            chordDistance(pq.surfacePoint, pa.surfacePoint, pb.surfacePoint),
            chordDistance(pm.surfacePoint, pa.surfacePoint, pb.surfacePoint),
            chordDistance(pr.surfacePoint, pa.surfacePoint, pb.surfacePoint)});
        const double angle = std::max({normalAngle(pa.surfaceNormal, pq.surfaceNormal),
            normalAngle(pq.surfaceNormal, pm.surfaceNormal), normalAngle(pm.surfaceNormal, pr.surfaceNormal),
            normalAngle(pr.surfaceNormal, pb.surfaceNormal), normalAngle(pa.surfaceNormal, pb.surfaceNormal)});
        const double arcLength = GCPnts_AbscissaPoint::Length(curve, a, b, parameters.tolerance.linear);
        const bool divide = arcLength > parameters.pointDistance + parameters.tolerance.linear ||
                            chordError > parameters.chordTolerance || angle > angleLimit;
        if (divide) {
            constexpr int maxSubdivisionDepth = 24;
            if (depth >= maxSubdivisionDepth || arcLength <= parameters.tolerance.linear)
                throw std::runtime_error("Adaptive sampling cannot meet chord/normal tolerances");
            refine(a, pa, mid, pm, depth + 1);
            refine(mid, pm, b, pb, depth + 1);
        } else {
            if (result.size() >= parameters.maxPoints)
                throw std::runtime_error("Path point limit exceeded during adaptive sampling");
            result.push_back(pb);
        }
    };
    for (std::size_t index = 1; index < sampleParameters.size(); ++index) {
        const PathPoint endpoint = evaluate(sampleParameters[index]);
        const PathPoint beginning = result.back();
        refine(sampleParameters[index - 1], beginning, sampleParameters[index], endpoint, 0);
    }
    return result;
}
} // namespace sps
