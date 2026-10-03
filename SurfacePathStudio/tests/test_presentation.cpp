#include "viewer/PathPresentation.h"
#include <AIS_ListOfInteractive.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Standard_Failure.hxx>
#include <V3d_Viewer.hxx>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int displayed(const Handle(AIS_InteractiveContext)& context) {
    AIS_ListOfInteractive objects;
    context->DisplayedObjects(objects);
    return objects.Extent();
}
sps::SurfacePath samplePath(bool includeProcess = true) {
    sps::SurfacePath path;
    path.sectionOffsets = {0.0};
    if (includeProcess) {
        sps::PathSegment process;
        process.type = sps::MotionType::Process;
        process.layerId = 0;
        for (double x : {0.0, 5.0, 10.0}) {
            sps::PathPoint point;
            point.surfacePoint = gp_Pnt(x, 0.0, 0.0);
            point.tcpPoint = gp_Pnt(x, 0.0, 2.0);
            point.faceId = 1;
            process.points.push_back(point);
        }
        path.segments.push_back(process);
    }
    sps::PathSegment transfer;
    transfer.type = sps::MotionType::Transfer;
    transfer.layerId = -1;
    for (double x : {10.0, 15.0}) {
        sps::PathPoint point;
        point.tcpPoint = gp_Pnt(x, 5.0, 2.0);
        transfer.points.push_back(point);
    }
    path.segments.push_back(transfer);
    return path;
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected one presentation test case");
        Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection, false);
        Handle(V3d_Viewer) viewer = new V3d_Viewer(driver);
        Handle(AIS_InteractiveContext) context = new AIS_InteractiveContext(viewer);
        sps::PathPresentation presentation(context);
        const std::string test = argv[1];
        if (test == "defaults") {
            presentation.show(samplePath());
            require(displayed(context) == 7, "Default path display must include machining and transfer lines, contact points, normals, and three TCP axes");
        } else if (test == "visibility") {
            presentation.setContactPointsVisible(true);
            presentation.setNormalsVisible(true);
            presentation.setFramesVisible(true);
            presentation.setReferenceFrameVisible(true);
            presentation.setSectionPlanesVisible(true);
            presentation.setLayerNumbersVisible(true);
            presentation.show(samplePath());
            const int allObjects = displayed(context);
            require(allObjects > 7, "Expected visible diagnostic components before hiding the path");
            presentation.setVisible(false);
            require(displayed(context) == 0, "Hiding a whole path must also erase contact points, normals, axes, reference frame, planes, and labels");
            presentation.setNormalsVisible(false);
            presentation.setFramesVisible(false);
            presentation.setPathVisible(false);
            require(displayed(context) == 0, "Changing component settings while a whole path is hidden must not display any AIS objects");
            presentation.setVisible(true);
            require(displayed(context) == allObjects - 6, "Showing a whole path must preserve disabled line, normal, and TCP axis component settings");
            presentation.setPathVisible(true);
            presentation.setNormalsVisible(true);
            presentation.setFramesVisible(true);
            require(displayed(context) == allObjects, "Restoring components must restore all original AIS objects");
        } else if (test == "nonprocess") {
            presentation.setContactPointsVisible(true);
            presentation.setNormalsVisible(true);
            presentation.setFramesVisible(true);
            presentation.show(samplePath(false));
            require(displayed(context) == 1, "Transfer points must not be displayed as machining contacts, CAD normals, or machining TCP axes");
        } else throw std::runtime_error("Unknown presentation test case");
        presentation.clear();
        require(displayed(context) == 0, "Clearing a path must remove all its AIS objects");
        std::cout << "presentation_" << test << " passed\n";
        return 0;
    } catch (const Standard_Failure& error) {
        std::cerr << error.GetMessageString() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
