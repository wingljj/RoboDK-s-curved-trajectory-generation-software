#include "ExampleModels.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <GeomAPI_PointsToBSplineSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <cmath>
#include <stdexcept>

namespace sps::examples {
namespace {
constexpr double pi = 3.14159265358979323846;

Handle(Geom_BSplineSurface) freeformSurface() {
    TColgp_Array2OfPnt grid(1, 7, 1, 7);
    for (int i = 1; i <= 7; ++i) {
        const double x = (i - 1) * 100.0 / 6.0;
        for (int j = 1; j <= 7; ++j) {
            const double y = (j - 1) * 80.0 / 6.0;
            const double z = 6 * std::sin(pi * x / 100) * std::cos(pi * y / 80)
                           + 3 * std::sin(3 * pi * x / 100) * std::sin(2 * pi * y / 80);
            grid.SetValue(i, j, gp_Pnt(x, y, z));
        }
    }
    GeomAPI_PointsToBSplineSurface interpolator;
    interpolator.Interpolate(grid);
    if (!interpolator.IsDone()) throw std::runtime_error("Unable to create BSpline example surface");
    return interpolator.Surface();
}
}

std::vector<Fixture> fixtures() {
    std::vector<Fixture> result;
    const TopoDS_Face plane = BRepBuilderAPI_MakeFace(
        gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 0, 100, 0, 80).Face();
    result.push_back({"Plane (100 x 80 mm)", "plane", plane, {}});

    PathParameters cylindricalParameters;
    cylindricalParameters.direction = gp_Vec(0, 0, 1);
    cylindricalParameters.referenceNormal = gp_Dir(1, 0, 0);
    const gp_Cylinder cylinder(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 30);
    result.push_back({"Cylinder patch (R30 mm)", "cylinder",
        BRepBuilderAPI_MakeFace(cylinder, -0.9, 0.9, 0, 80).Face(), cylindricalParameters});
    result.push_back({"Full cylinder (R30 mm)", "cylinder_full",
        BRepBuilderAPI_MakeFace(cylinder, 0, 2 * pi, 0, 80).Face(), cylindricalParameters});

    PathParameters sphericalParameters;
    sphericalParameters.direction = gp_Vec(0, 0, 1);
    sphericalParameters.referenceNormal = gp_Dir(1, 0, 0);
    result.push_back({"Trimmed sphere (R60 mm)", "sphere_trimmed",
        BRepBuilderAPI_MakeFace(gp_Sphere(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 60),
                                -0.75, 0.75, -0.55, 0.55).Face(), sphericalParameters});

    const Handle(Geom_BSplineSurface) surface = freeformSurface();
    double u0, u1, v0, v1;
    surface->Bounds(u0, u1, v0, v1);
    const double du = u1 - u0, dv = v1 - v0;
    u0 += 0.08 * du;
    u1 -= 0.08 * du;
    v0 += 0.1 * dv;
    v1 -= 0.1 * dv;
    result.push_back({"Trimmed undulating BSpline", "bspline_trimmed",
        BRepBuilderAPI_MakeFace(surface, u0, u1, v0, v1, 1e-7).Face(), {}});

    const double mid = (u0 + u1) / 2;
    const TopoDS_Face left = BRepBuilderAPI_MakeFace(surface, u0, mid, v0, v1, 1e-7).Face();
    const TopoDS_Face right = BRepBuilderAPI_MakeFace(surface, mid, u1, v0, v1, 1e-7).Face();
    BRepBuilderAPI_Sewing sewing(1e-6);
    sewing.Add(left);
    sewing.Add(right);
    sewing.Perform();
    if (sewing.SewedShape().IsNull()) throw std::runtime_error("Unable to sew adjacent BSpline example faces");
    result.push_back({"Two connected BSpline faces", "adjacent_bspline", sewing.SewedShape(), {}});

    BRepBuilderAPI_MakeFace holed(plane);
    TopoDS_Wire inner = BRepBuilderAPI_MakeWire(
        BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(50, 40, 0), gp_Dir(0, 0, 1)), 12))).Wire();
    inner.Reverse();
    holed.Add(inner);
    result.push_back({"Plane with circular inner hole (R12 mm)", "inner_hole", holed.Face(), {}});
    return result;
}
} // namespace sps::examples
