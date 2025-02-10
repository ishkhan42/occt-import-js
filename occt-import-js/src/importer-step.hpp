#pragma once

#include "importer-xcaf.hpp"

class ImporterStep : public ImporterXcaf
{
public:
    ImporterStep ();

    // Add method to access document
    Handle (TDocStd_Document) GetDocument () const { return document; }

private:
    virtual bool TransferToDocument (const std::vector<std::uint8_t>& fileContent) override;
};
