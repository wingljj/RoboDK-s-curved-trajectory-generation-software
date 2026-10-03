#include "SelectionManager.h"
#include "cad/CadDocument.h"
#include "geometry/SurfaceNormal.h"
#include <BRepAdaptor_Curve.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>

namespace sps {
SelectionManager::SelectionManager(QObject* parent) : QObject(parent) {}
void SelectionManager::setDocument(std::shared_ptr<CadDocument> document) {
    m_document = std::move(document);
    m_ids.clear();
    emit changed();
}
void SelectionManager::set(const std::vector<int>& ids) {
    std::vector<int> valid;
    if (m_document) {
        for (int id : ids) {
            if (id >= 1 && id <= m_document->faces.count() &&
                std::find(valid.begin(), valid.end(), id) == valid.end()) valid.push_back(id);
        }
    }
    if (m_ids != valid) { m_ids = std::move(valid); emit changed(); }
}
void SelectionManager::toggle(int id, bool additive) {
    if (!m_document || id < 1 || id > m_document->faces.count()) return;
    if (!additive) { set({id}); return; }
    auto ids = m_ids;
    auto item = std::find(ids.begin(), ids.end(), id);
    if (item == ids.end()) ids.push_back(id);
    else ids.erase(item);
    set(ids);
}
void SelectionManager::clear() { set({}); }
double SelectionManager::totalArea() const {
    double area = 0.0;
    if (m_document) for (int id : m_ids) area += m_document->faces.metadata(id).area;
    return area;
}

void SelectionManager::selectTangentFaces(double degrees) {
    if (!m_document || m_ids.empty() || !std::isfinite(degrees) || degrees < 0 || degrees > 180) return;
    const double cosine = std::cos(degrees * std::acos(-1.0) / 180.0);
    TopTools_IndexedDataMapOfShapeListOfShape neighbors;
    TopExp::MapShapesAndAncestors(m_document->shape, TopAbs_EDGE, TopAbs_FACE, neighbors);
    std::set<int> included(m_ids.begin(), m_ids.end());
    std::queue<int> pending;
    for (int id : m_ids) pending.push(id);
    auto tangentAcross = [&](const TopoDS_Face& a, const TopoDS_Face& b, const TopoDS_Edge& edge) {
        try {
            BRepAdaptor_Curve curve(edge);
            for (double fraction : {0.25, 0.5, 0.75}) {
                const double parameter = curve.FirstParameter() + fraction * (curve.LastParameter() - curve.FirstParameter());
                const gp_Pnt point = curve.Value(parameter);
                SurfaceEvaluation first, second;
                if (!evaluateSurfacePoint(a, point, {}, first) || !evaluateSurfacePoint(b, point, {}, second) ||
                    first.normal.Dot(second.normal) < cosine) return false;
            }
            return true;
        } catch (const Standard_Failure&) { return false; }
    };
    while (!pending.empty()) {
        const auto& face = m_document->faces.face(pending.front());
        pending.pop();
        for (TopExp_Explorer edges(face, TopAbs_EDGE); edges.More(); edges.Next()) {
            const auto& edge = TopoDS::Edge(edges.Current());
            const int entry = neighbors.FindIndex(edge);
            if (entry == 0) continue;
            for (TopTools_ListIteratorOfListOfShape it(neighbors.FindFromIndex(entry)); it.More(); it.Next()) {
                const auto& candidate = TopoDS::Face(it.Value());
                const int id = m_document->faces.idFor(candidate);
                if (id > 0 && included.find(id) == included.end() && tangentAcross(face, candidate, edge)) {
                    included.insert(id);
                    pending.push(id);
                }
            }
        }
    }
    set(std::vector<int>(included.begin(), included.end()));
}
} // namespace sps
