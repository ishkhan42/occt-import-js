#pragma once

#include "importer.hpp"
#include <TDocStd_Document.hxx>
#include <RWGltf_CafWriter.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <Message_ProgressRange.hxx>

class GltfExporter
{
public:
    GltfExporter (const std::string& filename, bool isBinary = false);

    bool Export (const Handle (TDocStd_Document)& document);
    bool ExportNode (const NodePtr& rootNode);

    void SetTransformationFormat (RWGltf_WriterTrsfFormat format);
    void SetForcedUVExport (bool force);

private:
    std::string mFilename;
    bool mIsBinary;
    RWGltf_WriterTrsfFormat mTransformFormat;
    bool mForceUVExport;
};
