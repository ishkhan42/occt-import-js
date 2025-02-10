#include "gltf-exporter.hpp"

GltfExporter::GltfExporter (const std::string& filename, bool isBinary) :
    mFilename (filename),
    mIsBinary (isBinary),
    mTransformFormat (RWGltf_WriterTrsfFormat_Compact),
    mForceUVExport (true)
{}

bool GltfExporter::Export (const Handle (TDocStd_Document)& document)
{
    if (document.IsNull ()) {
        return false;
    }

    auto writer = RWGltf_CafWriter (mFilename.c_str (), mIsBinary);
    writer.SetTransformationFormat (mTransformFormat);
    writer.SetForcedUVExport (mForceUVExport);
    writer.SetNodeNameFormat (RWMesh_NameFormat_ProductAndInstance);
    // writer.SetMeshNameFormat (RWMesh_NameFormat_ProductAndInstanceAndOcaf);
    writer.SetToEmbedTexturesInGlb (true);
    writer.SetParallel (true);

    TColStd_IndexedDataMapOfStringString fileInfo;
    Message_ProgressRange progressRange;

    try {
        return writer.Perform (document, fileInfo, progressRange);
    } catch (...) {
        return false;
    }
}

void GltfExporter::SetTransformationFormat (RWGltf_WriterTrsfFormat format)
{
    mTransformFormat = format;
}

void GltfExporter::SetForcedUVExport (bool force)
{
    mForceUVExport = force;
}
