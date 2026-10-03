#pragma once
#include "path/PathTypes.h"
#include <AIS_InteractiveContext.hxx>
#include <vector>

namespace sps {
class PathPresentation {
public:
    explicit PathPresentation(const Handle(AIS_InteractiveContext)& context);
    ~PathPresentation();
    void show(const SurfacePath& path);
    void clear();
    void setVisible(bool visible);
    void setPathVisible(bool visible);
    void setContactPointsVisible(bool visible);
    void setNormalsVisible(bool visible);
    void setFramesVisible(bool visible);
    void setReferenceFrameVisible(bool visible);
    void setSectionPlanesVisible(bool visible);
    void setLayerNumbersVisible(bool visible);
    void setHighlighted(bool highlighted);
private:
    void applyVisibility();
    Handle(AIS_InteractiveContext) m_context;
    std::vector<Handle(AIS_InteractiveObject)> m_paths, m_contacts, m_normals, m_frames, m_reference, m_planes, m_layers;
    bool m_visible = true;
    bool m_showPath = true, m_showContacts = true, m_showNormals = true, m_showFrames = true;
    bool m_showReference = false, m_showPlanes = false, m_showLayers = false;
    bool m_highlighted = false;
};
} // namespace sps
