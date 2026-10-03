#include "ExampleModels.h"
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "examples/models";
        std::filesystem::create_directories(output);
        const auto fixtures = sps::examples::fixtures();
        for (const auto& fixture : fixtures) {
            STEPControl_Writer writer;
            Interface_Static::SetCVal("write.step.unit", "MM");
            if (writer.Transfer(fixture.shape, STEPControl_AsIs) != IFSelect_RetDone)
                throw std::runtime_error("STEP transfer failed: " + fixture.name);
            const auto filename = output / (fixture.fileStem + ".step");
            if (writer.Write(filename.string().c_str()) != IFSelect_RetDone)
                throw std::runtime_error("STEP write failed: " + filename.string());
            std::cout << fixture.name << ": " << filename.string() << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Open CASCADE failed to generate the examples\n";
        return 1;
    }
}
