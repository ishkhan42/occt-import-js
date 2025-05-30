#ifdef EMSCRIPTEN

#include "js-interface.hpp"
#include "importer-step.hpp"
#include "importer-iges.hpp"
#include "importer-brep.hpp"
#include <RWGltf_CafWriter.hxx>
#include <Message_ProgressRange.hxx>
#include <TCollection_AsciiString.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <emscripten/bind.h>
#include "gltf-exporter.hpp"
#include <chrono> // Added for timing
#include <iostream> // Added for logging

class HierarchyWriter
{
public:
    HierarchyWriter (emscripten::val& meshesArr) :
        mMeshesArr (meshesArr),
        mMeshCount (0)
    {}

    void WriteNode (const NodePtr& node, emscripten::val& nodeObj)
    {
        nodeObj.set ("name", node->GetName ());

        emscripten::val nodeMeshesArr (emscripten::val::array ());
        WriteMeshes (node, nodeMeshesArr);
        nodeObj.set ("meshes", nodeMeshesArr);

        std::vector<NodePtr> children = node->GetChildren ();
        emscripten::val childrenArr (emscripten::val::array ());
        for (int childIndex = 0; childIndex < children.size (); childIndex++) {
            const NodePtr& child = children[childIndex];
            emscripten::val childNodeObj (emscripten::val::object ());
            WriteNode (child, childNodeObj);
            childrenArr.set (childIndex, childNodeObj);
        }
        nodeObj.set ("children", childrenArr);
    }

private:
    void WriteMeshes (const NodePtr& node, emscripten::val& nodeMeshesArr)
    {
        if (!node->IsMeshNode ()) {
            return;
        }

        int nodeMeshCount = 0;
        node->EnumerateMeshes ([&](const Mesh& mesh) {
            emscripten::val meshObj (emscripten::val::object ());
            meshObj.set ("name", mesh.GetName ());

            int vertexCount = 0;
            int normalCount = 0;
            int triangleCount = 0;
            int brepFaceCount = 0;

            emscripten::val positionArr (emscripten::val::array ());
            emscripten::val normalArr (emscripten::val::array ());
            emscripten::val indexArr (emscripten::val::array ());
            emscripten::val brepFaceArr (emscripten::val::array ());

            mesh.EnumerateFaces ([&](const Face& face) {
                int triangleOffset = triangleCount;
                int vertexOffset = vertexCount;
                face.EnumerateVertices ([&](double x, double y, double z) {
                    positionArr.set (vertexCount * 3, x);
                    positionArr.set (vertexCount * 3 + 1, y);
                    positionArr.set (vertexCount * 3 + 2, z);
                    vertexCount += 1;
                });
                face.EnumerateNormals ([&](double x, double y, double z) {
                    normalArr.set (normalCount * 3, x);
                    normalArr.set (normalCount * 3 + 1, y);
                    normalArr.set (normalCount * 3 + 2, z);
                    normalCount += 1;
                });
                face.EnumerateTriangles ([&](int v0, int v1, int v2) {
                    indexArr.set (triangleCount * 3, vertexOffset + v0);
                    indexArr.set (triangleCount * 3 + 1, vertexOffset + v1);
                    indexArr.set (triangleCount * 3 + 2, vertexOffset + v2);
                    triangleCount += 1;
                });
                emscripten::val brepFaceObj (emscripten::val::object ());
                brepFaceObj.set ("first", triangleOffset);
                brepFaceObj.set ("last", triangleCount - 1);
                Color faceColor;
                if (face.GetColor (faceColor)) {
                    emscripten::val colorArr (emscripten::val::array ());
                    colorArr.set (0, faceColor.r);
                    colorArr.set (1, faceColor.g);
                    colorArr.set (2, faceColor.b);
                    brepFaceObj.set ("color", colorArr);
                } else {
                    brepFaceObj.set ("color", emscripten::val::null ());
                }
                brepFaceArr.set (brepFaceCount, brepFaceObj);
                brepFaceCount += 1;
            });

            emscripten::val attributesObj (emscripten::val::object ());

            emscripten::val positionObj (emscripten::val::object ());
            positionObj.set ("array", positionArr);
            attributesObj.set ("position", positionObj);

            if (vertexCount == normalCount) {
                emscripten::val normalObj (emscripten::val::object ());
                normalObj.set ("array", normalArr);
                attributesObj.set ("normal", normalObj);
            }

            emscripten::val indexObj (emscripten::val::object ());
            indexObj.set ("array", indexArr);

            meshObj.set ("attributes", attributesObj);
            meshObj.set ("index", indexObj);

            Color meshColor;
            if (mesh.GetColor (meshColor)) {
                emscripten::val colorArr (emscripten::val::array ());
                colorArr.set (0, meshColor.r);
                colorArr.set (1, meshColor.g);
                colorArr.set (2, meshColor.b);
                meshObj.set ("color", colorArr);
            }

            meshObj.set ("brep_faces", brepFaceArr);

            mMeshesArr.set (mMeshCount, meshObj);
            nodeMeshesArr.set (nodeMeshCount, mMeshCount);
            mMeshCount += 1;
            nodeMeshCount += 1;
        });
    }

    emscripten::val& mMeshesArr;
    int mMeshCount;
};

static void EnumerateNodeMeshes (const NodePtr& node, const std::function<void (const Mesh&)>& onMesh)
{
    if (node->IsMeshNode ()) {
        node->EnumerateMeshes (onMesh);
    }
    std::vector<NodePtr> children = node->GetChildren ();
    for (const NodePtr& child : children) {
        EnumerateNodeMeshes (child, onMesh);
    }
}

static emscripten::val ImportFile (ImporterPtr importer, const emscripten::val& buffer, const ImportParams& params)
{
    emscripten::val resultObj (emscripten::val::object ());

    const std::vector<uint8_t>& bufferArr = emscripten::vecFromJSArray<std::uint8_t> (buffer);
    Importer::Result importResult = importer->LoadFile (bufferArr, params);
    resultObj.set ("success", importResult == Importer::Result::Success);
    if (importResult != Importer::Result::Success) {
        return resultObj;
    }

    int meshIndex = 0;
    emscripten::val rootNodeObj (emscripten::val::object ());
    emscripten::val meshesArr (emscripten::val::array ());
    NodePtr rootNode = importer->GetRootNode ();

    HierarchyWriter hierarchyWriter (meshesArr);
    hierarchyWriter.WriteNode (rootNode, rootNodeObj);

    resultObj.set ("root", rootNodeObj);
    resultObj.set ("meshes", meshesArr);
    return resultObj;
}

static ImportParams GetImportParams (const emscripten::val& paramsVal)
{
    ImportParams params;
    if (paramsVal.isUndefined () || paramsVal.isNull ()) {
        return params;
    }

    if (paramsVal.hasOwnProperty ("linearUnit")) {
        emscripten::val linearUnit = paramsVal["linearUnit"];
        std::string linearUnitStr = linearUnit.as<std::string> ();
        if (linearUnitStr == "millimeter") {
            params.linearUnit = ImportParams::LinearUnit::Millimeter;
        } else if (linearUnitStr == "centimeter") {
            params.linearUnit = ImportParams::LinearUnit::Centimeter;
        } else if (linearUnitStr == "meter") {
            params.linearUnit = ImportParams::LinearUnit::Meter;
        } else if (linearUnitStr == "inch") {
            params.linearUnit = ImportParams::LinearUnit::Inch;
        } else if (linearUnitStr == "foot") {
            params.linearUnit = ImportParams::LinearUnit::Foot;
        }
    }

    if (paramsVal.hasOwnProperty ("linearDeflectionType")) {
        emscripten::val linearDeflectionType = paramsVal["linearDeflectionType"];
        std::string linearDeflectionTypeStr = linearDeflectionType.as<std::string> ();
        if (linearDeflectionTypeStr == "bounding_box_ratio") {
            params.linearDeflectionType = ImportParams::LinearDeflectionType::BoundingBoxRatio;
        } else if (linearDeflectionTypeStr == "absolute_value") {
            params.linearDeflectionType = ImportParams::LinearDeflectionType::AbsoluteValue;
        }
    }

    if (paramsVal.hasOwnProperty ("linearDeflection")) {
        emscripten::val linearDeflection = paramsVal["linearDeflection"];
        params.linearDeflection = linearDeflection.as<double> ();
    }

    if (paramsVal.hasOwnProperty ("angularDeflection")) {
        emscripten::val angularDeflection = paramsVal["angularDeflection"];
        params.angularDeflection = angularDeflection.as<double> ();
    }

    return params;
}

emscripten::val ReadStepFile (const emscripten::val& buffer, const emscripten::val& params)
{
    ImporterPtr importer = std::make_shared<ImporterStep> ();
    ImportParams importParams = GetImportParams (params);
    return ImportFile (importer, buffer, importParams);
}

emscripten::val ReadStepFileGLTF (const emscripten::val& buffer, const emscripten::val& output_name, const emscripten::val& params)
{
    auto func_total_start_time = std::chrono::high_resolution_clock::now ();
    std::cout << "[ReadStepFileGLTF] Execution started." << std::endl;

    ImporterPtr importer = std::make_shared<ImporterStep> ();

    auto get_params_start_time = std::chrono::high_resolution_clock::now ();
    ImportParams importParams = GetImportParams (params);
    auto get_params_end_time = std::chrono::high_resolution_clock::now ();
    std::cout << "[ReadStepFileGLTF] GetImportParams took: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(get_params_end_time - get_params_start_time).count ()
        << "ms" << std::endl;

    emscripten::val resultObj (emscripten::val::object ());
    std::string outputName = output_name.as<std::string> () + ".gltf";
    std::string outputNameBin = output_name.as<std::string> () + ".bin";

    const std::vector<uint8_t>& bufferArr = emscripten::vecFromJSArray<std::uint8_t> (buffer);

    auto load_file_start_time = std::chrono::high_resolution_clock::now ();
    Importer::Result importResult = importer->LoadFile (bufferArr, importParams);
    auto load_file_end_time = std::chrono::high_resolution_clock::now ();
    std::cout << "[ReadStepFileGLTF] importer->LoadFile took: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(load_file_end_time - load_file_start_time).count ()
        << "ms" << std::endl;

    resultObj.set ("success", importResult == Importer::Result::Success);

    if (importResult != Importer::Result::Success) {
        std::cout << "[ReadStepFileGLTF] Import failed." << std::endl;
        auto func_total_end_time_failure = std::chrono::high_resolution_clock::now ();
        std::cout << "[ReadStepFileGLTF] Total execution time (failure): "
            << std::chrono::duration_cast<std::chrono::milliseconds>(func_total_end_time_failure - func_total_start_time).count ()
            << "ms" << std::endl;
        return resultObj;
    }

    NodePtr rootNode = importer->GetRootNode ();

    // Perform traversal to ensure triangulation without emscripten::val overhead
    auto processing_start_time = std::chrono::high_resolution_clock::now ();
    EnumerateNodeMeshes (rootNode, [](const Mesh& mesh) {
        // Enumerate mesh components to ensure OCCT processes them for triangulation.
        // The act of enumeration itself is what triggers the necessary internal
        // processing in OCCT if triangulation hasn't occurred yet.
        mesh.EnumerateFaces ([](const Face& face) {
            // Iterate through vertices, normals, and triangles.
            // The lambda bodies are empty because we only need to trigger the enumeration.
            // (void) casts suppress unused variable warnings.
            face.EnumerateVertices ([](double x, double y, double z) { (void) x; (void) y; (void) z; });
            face.EnumerateNormals ([](double x, double y, double z) { (void) x; (void) y; (void) z; });
            face.EnumerateTriangles ([](int v0, int v1, int v2) { (void) v0; (void) v1; (void) v2; });
        });
    });
    auto processing_end_time = std::chrono::high_resolution_clock::now ();
    std::cout << "[ReadStepFileGLTF] Triangulation processing (EnumerateNodeMeshes) took: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(processing_end_time - processing_start_time).count ()
        << "ms" << std::endl;

    // Cast importer to ImporterStep to access document
    auto cast_start_time = std::chrono::high_resolution_clock::now ();
    auto importerStep = std::dynamic_pointer_cast<ImporterStep>(importer);
    auto cast_end_time = std::chrono::high_resolution_clock::now ();
    std::cout << "[ReadStepFileGLTF] dynamic_pointer_cast<ImporterStep> took: "
        << std::chrono::duration_cast<std::chrono::microseconds>(cast_end_time - cast_start_time).count ()
        << "us" << std::endl;

    if (!importerStep) {
        resultObj.set ("success", false);
        std::cout << "[ReadStepFileGLTF] Failed to cast ImporterPtr to ImporterStepPtr." << std::endl;
        auto func_total_end_time_cast_failure = std::chrono::high_resolution_clock::now ();
        std::cout << "[ReadStepFileGLTF] Total execution time (cast failure): "
            << std::chrono::duration_cast<std::chrono::milliseconds>(func_total_end_time_cast_failure - func_total_start_time).count ()
            << "ms" << std::endl;
        return resultObj;
    }

    // Export to GLTF
    auto export_gltf_start_time = std::chrono::high_resolution_clock::now ();
    GltfExporter exporter (outputName, true);
    bool exportSuccess = exporter.Export (importerStep->GetDocument ());
    auto export_gltf_end_time = std::chrono::high_resolution_clock::now ();
    std::cout << "[ReadStepFileGLTF] GltfExporter::Export took: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(export_gltf_end_time - export_gltf_start_time).count ()
        << "ms" << std::endl;

    resultObj.set ("success", exportSuccess);
    if (exportSuccess) {
        resultObj.set ("gla", outputName);
        resultObj.set ("glb", outputNameBin);
    }

    auto func_total_end_time_success = std::chrono::high_resolution_clock::now ();
    std::cout << "[ReadStepFileGLTF] Execution finished. Total time: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(func_total_end_time_success - func_total_start_time).count ()
        << "ms" << std::endl;

    return resultObj;
}

emscripten::val ReadIgesFile (const emscripten::val& buffer, const emscripten::val& params)
{
    ImporterPtr importer = std::make_shared<ImporterIges> ();
    ImportParams importParams = GetImportParams (params);
    return ImportFile (importer, buffer, importParams);
}

emscripten::val ReadBrepFile (const emscripten::val& buffer, const emscripten::val& params)
{
    ImporterPtr importer = std::make_shared<ImporterBrep> ();
    ImportParams importParams = GetImportParams (params);
    return ImportFile (importer, buffer, importParams);
}

emscripten::val ReadFile (const std::string& format, const emscripten::val& buffer, const emscripten::val& params)
{
    if (format == "step") {
        return ReadStepFile (buffer, params);
    } else if (format == "iges") {
        return ReadIgesFile (buffer, params);
    } else if (format == "brep") {
        return ReadBrepFile (buffer, params);
    } else {
        emscripten::val resultObj (emscripten::val::object ());
        resultObj.set ("success", false);
        return resultObj;
    }
}

EMSCRIPTEN_BINDINGS (occtimportjs)
{
    emscripten::function<emscripten::val, const std::string&, const emscripten::val&, const emscripten::val&> ("ReadFile", &ReadFile);

    emscripten::function<emscripten::val, const emscripten::val&, const emscripten::val&> ("ReadStepFile", &ReadStepFile);
    emscripten::function<emscripten::val, const emscripten::val&, const emscripten::val&> ("ReadStepFileGLTF", &ReadStepFileGLTF);
    emscripten::function<emscripten::val, const emscripten::val&, const emscripten::val&> ("ReadIgesFile", &ReadIgesFile);
    emscripten::function<emscripten::val, const emscripten::val&, const emscripten::val&> ("ReadBrepFile", &ReadBrepFile);
}

#endif
