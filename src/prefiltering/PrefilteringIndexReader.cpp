#include "PrefilteringIndexReader.h"
#include "DBWriter.h"
#include "Prefiltering.h"
#include "ExtendedSubstitutionMatrix.h"
#include "FileUtil.h"
#include "IndexBuilder.h"

const char*  PrefilteringIndexReader::CURRENT_VERSION = "7";
unsigned int PrefilteringIndexReader::VERSION = 0;
unsigned int PrefilteringIndexReader::META = 1;
unsigned int PrefilteringIndexReader::SCOREMATRIXNAME = 2;
unsigned int PrefilteringIndexReader::SCOREMATRIX2MER = 3;
unsigned int PrefilteringIndexReader::SCOREMATRIX3MER = 4;
unsigned int PrefilteringIndexReader::DBRINDEX = 5;
unsigned int PrefilteringIndexReader::HDRINDEX = 6;

unsigned int PrefilteringIndexReader::ENTRIES = 7;
unsigned int PrefilteringIndexReader::ENTRIESOFFSETS = 8;
unsigned int PrefilteringIndexReader::ENTRIESNUM = 9;
unsigned int PrefilteringIndexReader::SEQCOUNT = 10;
unsigned int PrefilteringIndexReader::MASKEDSEQINDEXDATA = 11;
unsigned int PrefilteringIndexReader::SEQINDEXDATASIZE = 12;
unsigned int PrefilteringIndexReader::SEQINDEXSEQOFFSET = 13;
unsigned int PrefilteringIndexReader::UNMASKEDSEQINDEXDATA = 14;
unsigned int PrefilteringIndexReader::GENERATOR = 15;

extern const char* version;

bool PrefilteringIndexReader::checkIfIndexFile(DBReader<unsigned int>* reader) {
    char * version = reader->getDataByDBKey(VERSION);
    if(version == NULL){
        return false;
    }
    return (strncmp(version, CURRENT_VERSION, strlen(CURRENT_VERSION)) == 0 ) ? true : false;
}

void PrefilteringIndexReader::createIndexFile(const std::string &outDB, DBReader<unsigned int> *dbr, DBReader<unsigned int> *hdbr,
                                              BaseMatrix * subMat, int maxSeqLen, bool hasSpacedKmer,
                                              bool compBiasCorrection, int alphabetSize, int kmerSize,
                                              int maskMode, int kmerThr) {
    std::string outIndexName(outDB);
    std::string spaced = (hasSpacedKmer == true) ? "s" : "";
    outIndexName.append(".").append(spaced).append("k").append(SSTR(kmerSize));

    DBWriter writer(outIndexName.c_str(), std::string(outIndexName).append(".index").c_str(), 1, DBWriter::BINARY_MODE);
    writer.open();

    const int seqType = dbr->getDbtype();
    if (seqType != Sequence::HMM_PROFILE && seqType != Sequence::PROFILE_STATE_SEQ) {
        int alphabetSize = subMat->alphabetSize;
        subMat->alphabetSize = subMat->alphabetSize-1;
        ScoreMatrix *s3 = ExtendedSubstitutionMatrix::calcScoreMatrix(*subMat, 3);
        ScoreMatrix *s2 = ExtendedSubstitutionMatrix::calcScoreMatrix(*subMat, 2);
        subMat->alphabetSize = alphabetSize;
        char* serialized3mer = ScoreMatrix::serialize(*s3);
        Debug(Debug::INFO) << "Write SCOREMATRIX3MER (" << SCOREMATRIX3MER << ")\n";
        writer.writeData(serialized3mer, ScoreMatrix::size(*s3), SCOREMATRIX3MER, 0);
        writer.alignToPageSize();
        free(serialized3mer);
        ScoreMatrix::cleanup(s3);

        char* serialized2mer = ScoreMatrix::serialize(*s2);
        Debug(Debug::INFO) << "Write SCOREMATRIX2MER (" << SCOREMATRIX2MER << ")\n";
        writer.writeData(serialized2mer, ScoreMatrix::size(*s2), SCOREMATRIX2MER, 0);
        writer.alignToPageSize();
        free(serialized2mer);
        ScoreMatrix::cleanup(s2);
    }

    Sequence seq(maxSeqLen, seqType, subMat, kmerSize, hasSpacedKmer, compBiasCorrection);
    // remove x (not needed in index)
    int adjustAlphabetSize = (seqType == Sequence::NUCLEOTIDES || seqType == Sequence::AMINO_ACIDS)
                             ? alphabetSize -1: alphabetSize;

    IndexTable *indexTable = new IndexTable(adjustAlphabetSize, kmerSize, false);
    SequenceLookup *maskedLookup = NULL;
    SequenceLookup *unmaskedLookup = NULL;
    IndexBuilder::fillDatabase(indexTable,
                               (maskMode == 1 || maskMode == 2) ? &maskedLookup : NULL,
                               (maskMode == 0 || maskMode == 2) ? &unmaskedLookup : NULL,
                               *subMat, &seq, dbr, 0, dbr->getSize(), kmerThr);

    SequenceLookup *sequenceLookup = maskedLookup;
    if (sequenceLookup == NULL) {
        sequenceLookup = unmaskedLookup;
    }
    if (sequenceLookup == NULL) {
        Debug(Debug::ERROR) << "Invalid mask mode. No sequence lookup created!\n";
        EXIT(EXIT_FAILURE);
    }

    indexTable->printStatistics(subMat->int2aa);

    // save the entries
    Debug(Debug::INFO) << "Write ENTRIES (" << ENTRIES << ")\n";
    char *entries = (char *) indexTable->getEntries();
    size_t entriesSize = indexTable->getTableEntriesNum() * indexTable->getSizeOfEntry();
    writer.writeData(entries, entriesSize, ENTRIES, 0);
    writer.alignToPageSize();

    // save the size
    Debug(Debug::INFO) << "Write ENTRIESOFFSETS (" << ENTRIESOFFSETS << ")\n";

    char *offsets = (char*)indexTable->getOffsets();
    size_t offsetsSize = (indexTable->getTableSize() + 1) * sizeof(size_t);
    writer.writeData(offsets, offsetsSize, ENTRIESOFFSETS, 0);
    writer.alignToPageSize();
    indexTable->deleteEntries();

    Debug(Debug::INFO) << "Write SEQINDEXDATASIZE (" << SEQINDEXDATASIZE << ")\n";
    int64_t seqindexDataSize = sequenceLookup->getDataSize();
    char *seqindexDataSizePtr = (char *) &seqindexDataSize;
    writer.writeData(seqindexDataSizePtr, 1 * sizeof(int64_t), SEQINDEXDATASIZE, 0);
    writer.alignToPageSize();

    size_t *sequenceOffsets = sequenceLookup->getOffsets();
    size_t sequenceCount = sequenceLookup->getSequenceCount();
    Debug(Debug::INFO) << "Write SEQINDEXSEQOFFSET (" << SEQINDEXSEQOFFSET << ")\n";
    writer.writeData((char *) sequenceOffsets, (sequenceCount + 1) * sizeof(size_t), SEQINDEXSEQOFFSET, 0);
    writer.alignToPageSize();

    if (maskedLookup != NULL) {
        Debug(Debug::INFO) << "Write MASKEDSEQINDEXDATA (" << MASKEDSEQINDEXDATA << ")\n";
        writer.writeData(maskedLookup->getData(), (maskedLookup->getDataSize() + 1) * sizeof(char), MASKEDSEQINDEXDATA, 0);
        writer.alignToPageSize();
        delete maskedLookup;
    }

    if (unmaskedLookup != NULL) {
        Debug(Debug::INFO) << "Write UNMASKEDSEQINDEXDATA (" << UNMASKEDSEQINDEXDATA << ")\n";
        writer.writeData(unmaskedLookup->getData(), (unmaskedLookup->getDataSize() + 1) * sizeof(char), UNMASKEDSEQINDEXDATA, 0);
        writer.alignToPageSize();
        delete unmaskedLookup;
    }

    // meta data
    // ENTRIESNUM
    Debug(Debug::INFO) << "Write ENTRIESNUM (" << ENTRIESNUM << ")\n";
    uint64_t entriesNum = indexTable->getTableEntriesNum();
    char *entriesNumPtr = (char *) &entriesNum;
    writer.writeData(entriesNumPtr, 1 * sizeof(uint64_t), ENTRIESNUM, 0);
    writer.alignToPageSize();
    // SEQCOUNT
    Debug(Debug::INFO) << "Write SEQCOUNT (" << SEQCOUNT << ")\n";
    size_t tablesize = {indexTable->getSize()};
    char *tablesizePtr = (char *) &tablesize;
    writer.writeData(tablesizePtr, 1 * sizeof(size_t), SEQCOUNT, 0);
    writer.alignToPageSize();

    delete indexTable;

    Debug(Debug::INFO) << "Write META (" << META << ")\n";
    int mask = maskMode > 0;
    int spacedKmer = (hasSpacedKmer) ? 1 : 0;
    int headers = (hdbr != NULL) ? 1 : 0;
    int metadata[] = {kmerSize, alphabetSize, mask, spacedKmer, kmerThr, seqType, headers};
    char *metadataptr = (char *) &metadata;
    writer.writeData(metadataptr, sizeof(metadata), META, 0);
    writer.alignToPageSize();
    printMeta(metadata);

    Debug(Debug::INFO) << "Write SCOREMATRIXNAME (" << SCOREMATRIXNAME << ")\n";
    writer.writeData(subMat->getMatrixName().c_str(), subMat->getMatrixName().length(), SCOREMATRIXNAME, 0);
    writer.alignToPageSize();

    Debug(Debug::INFO) << "Write VERSION (" << VERSION << ")\n";
    writer.writeData((char *) CURRENT_VERSION, strlen(CURRENT_VERSION) * sizeof(char), VERSION, 0);
    writer.alignToPageSize();

    Debug(Debug::INFO) << "Write DBRINDEX (" << DBRINDEX << ")\n";
    char* data = DBReader<unsigned int>::serialize(*dbr);
    writer.writeData(data, DBReader<unsigned int>::indexMemorySize(*dbr), DBRINDEX, 0);
    writer.alignToPageSize();
    free(data);

    if (hdbr != NULL) {
        Debug(Debug::INFO) << "Write HDRINDEX (" << HDRINDEX << ")\n";
        data = DBReader<unsigned int>::serialize(*hdbr);
        writer.writeData(data, DBReader<unsigned int>::indexMemorySize(*hdbr), HDRINDEX, 0);
        writer.alignToPageSize();
        free(data);
    }

    Debug(Debug::INFO) << "Write GENERATOR (" << GENERATOR << ")\n";
    writer.writeData(version, strlen(version), GENERATOR, 0);
    writer.alignToPageSize();

    writer.close();
    Debug(Debug::INFO) << "Done. \n";
}

DBReader<unsigned int> *PrefilteringIndexReader::openNewHeaderReader(DBReader<unsigned int>*dbr, const char* dataFileName, bool touch) {
    size_t id = dbr->getId(HDRINDEX);
    char *data = dbr->getData(id);
    if (touch) {
        dbr->touchData(id);
    }

    DBReader<unsigned int> *reader = DBReader<unsigned int>::unserialize(data);
    reader->setDataFile(dataFileName);
    reader->open(DBReader<unsigned int>::NOSORT);

    return reader;
}

DBReader<unsigned int> *PrefilteringIndexReader::openNewReader(DBReader<unsigned int>*dbr, bool touch) {
    size_t id = dbr->getId(DBRINDEX);
    char *data = dbr->getData(id);
    if (touch) {
        dbr->touchData(id);
    }

    DBReader<unsigned int> *reader = DBReader<unsigned int>::unserialize(data);
    reader->open(DBReader<unsigned int>::NOSORT);

    return reader;
}

SequenceLookup *PrefilteringIndexReader::getMaskedSequenceLookup(DBReader<unsigned int> *dbr, bool touch) {
    size_t id;
    if ((id = dbr->getId(MASKEDSEQINDEXDATA)) == UINT_MAX) {
        return NULL;
    }

    char * seqData = dbr->getData(id);

    size_t seqOffsetsId = dbr->getId(SEQINDEXSEQOFFSET);
    char * seqOffsetsData = dbr->getData(seqOffsetsId);

    size_t seqDataSizeId = dbr->getId(SEQINDEXDATASIZE);
    int64_t seqDataSize = *((int64_t *)dbr->getData(seqDataSizeId));

    size_t sequenceCountId = dbr->getId(SEQCOUNT);
    size_t sequenceCount = *((size_t *)dbr->getData(sequenceCountId));

    if (touch) {
        dbr->touchData(id);
        dbr->touchData(seqOffsetsId);
    }

    SequenceLookup *sequenceLookup = new SequenceLookup(sequenceCount);
    sequenceLookup->initLookupByExternalData(seqData, seqDataSize, (size_t *) seqOffsetsData);

    return sequenceLookup;
}

SequenceLookup *PrefilteringIndexReader::getUnmaskedSequenceLookup(DBReader<unsigned int>*dbr, bool touch) {
    size_t id;
    if ((id = dbr->getId(UNMASKEDSEQINDEXDATA)) == UINT_MAX) {
        return NULL;
    }

    char * seqData = dbr->getData(id);

    size_t seqOffsetsId = dbr->getId(SEQINDEXSEQOFFSET);
    char * seqOffsetsData = dbr->getData(seqOffsetsId);

    size_t seqDataSizeId = dbr->getId(SEQINDEXDATASIZE);
    int64_t seqDataSize = *((int64_t *)dbr->getData(seqDataSizeId));

    size_t sequenceCountId = dbr->getId(SEQCOUNT);
    size_t sequenceCount = *((size_t *)dbr->getData(sequenceCountId));

    if (touch) {
        dbr->touchData(id);
        dbr->touchData(seqOffsetsId);
    }

    SequenceLookup *sequenceLookup = new SequenceLookup(sequenceCount);
    sequenceLookup->initLookupByExternalData(seqData, seqDataSize, (size_t *) seqOffsetsData);

    return sequenceLookup;
}

IndexTable *PrefilteringIndexReader::generateIndexTable(DBReader<unsigned int> *dbr, bool touch) {
    PrefilteringIndexData data = getMetadata(dbr);
    IndexTable *retTable;
    int adjustAlphabetSize;
    if (data.seqType == Sequence::NUCLEOTIDES || data.seqType == Sequence::AMINO_ACIDS) {
        adjustAlphabetSize = data.alphabetSize - 1;
    } else {
        adjustAlphabetSize = data.alphabetSize;
    }
    retTable = new IndexTable(adjustAlphabetSize, data.kmerSize, true);

    size_t entriesNumId = dbr->getId(ENTRIESNUM);
    int64_t entriesNum = *((int64_t *)dbr->getData(entriesNumId));
    size_t sequenceCountId = dbr->getId(SEQCOUNT);
    size_t sequenceCount = *((size_t *)dbr->getData(sequenceCountId));

    size_t entriesDataId = dbr->getId(ENTRIES);
    char *entriesData = dbr->getData(entriesDataId);

    size_t entriesOffsetsDataId = dbr->getId(ENTRIESOFFSETS);
    char *entriesOffsetsData = dbr->getData(entriesOffsetsDataId);

    if (touch) {
        dbr->touchData(entriesNumId);
        dbr->touchData(sequenceCountId);
        dbr->touchData(entriesDataId);
        dbr->touchData(entriesOffsetsDataId);
    }

    retTable->initTableByExternalData(sequenceCount, entriesNum, (IndexEntryLocal*) entriesData, (size_t *)entriesOffsetsData);
    return retTable;
}

void PrefilteringIndexReader::printMeta(int *metadata_tmp) {
    Debug(Debug::INFO) << "KmerSize:     " << metadata_tmp[0] << "\n";
    Debug(Debug::INFO) << "AlphabetSize: " << metadata_tmp[1] << "\n";
    Debug(Debug::INFO) << "Masked:       " << metadata_tmp[2] << "\n";
    Debug(Debug::INFO) << "Spaced:       " << metadata_tmp[3] << "\n";
    Debug(Debug::INFO) << "KmerScore:    " << metadata_tmp[4] << "\n";
    Debug(Debug::INFO) << "SequenceType: " << metadata_tmp[5] << "\n";
    Debug(Debug::INFO) << "Headers:      " << metadata_tmp[6] << "\n";
}

void PrefilteringIndexReader::printSummary(DBReader<unsigned int> *dbr) {
    Debug(Debug::INFO) << "Index version: " << dbr->getDataByDBKey(VERSION) << "\n";

    size_t id;
    if ((id = dbr->getId(GENERATOR)) != UINT_MAX) {
        Debug(Debug::INFO) << "Generated by:  " << dbr->getData(id) << "\n";
    }

    int *meta = (int *)dbr->getDataByDBKey(META);
    printMeta(meta);

    Debug(Debug::INFO) << "ScoreMatrix:  " << dbr->getDataByDBKey(SCOREMATRIXNAME) << "\n";
}

PrefilteringIndexData PrefilteringIndexReader::getMetadata(DBReader<unsigned int> *dbr) {
    int *meta = (int *)dbr->getDataByDBKey(META);

    PrefilteringIndexData data;
    data.kmerSize = meta[0];
    data.alphabetSize = meta[1];
    data.mask = meta[2];
    data.spacedKmer = meta[3];
    data.kmerThr = meta[4];
    data.seqType = meta[5];
    data.headers = meta[6];

    return data;
}

std::string PrefilteringIndexReader::getSubstitutionMatrixName(DBReader<unsigned int> *dbr) {
    return std::string(dbr->getDataByDBKey(SCOREMATRIXNAME));
}
//
ScoreMatrix *PrefilteringIndexReader::get2MerScoreMatrix(DBReader<unsigned int> *dbr, bool touch) {
    PrefilteringIndexData meta = getMetadata(dbr);
    size_t id = dbr->getId(SCOREMATRIX2MER);
    if (id == UINT_MAX) {
        return NULL;
    }

    char *data = dbr->getData(id);

    if (touch) {
        dbr->touchData(id);
    }

    return ScoreMatrix::unserialize(data, meta.alphabetSize-1, 2);
}

ScoreMatrix *PrefilteringIndexReader::get3MerScoreMatrix(DBReader<unsigned int> *dbr, bool touch) {
    PrefilteringIndexData meta = getMetadata(dbr);
    size_t id = dbr->getId(SCOREMATRIX3MER);
    if (id == UINT_MAX) {
        return NULL;
    }

    char *data = dbr->getData(id);

    if (touch) {
        dbr->touchData(id);
    }

    // remove x (not needed in index)
    return ScoreMatrix::unserialize(data, meta.alphabetSize-1, 3);
}

std::string PrefilteringIndexReader::searchForIndex(const std::string &pathToDB) {
    for (size_t spaced = 0; spaced < 2; spaced++) {
        for (size_t k = 5; k <= 7; k++) {
            std::string outIndexName(pathToDB); // db.sk6
            std::string s = (spaced == true) ? "s" : "";
            outIndexName.append(".").append(s).append("k").append(SSTR(k));
            if (FileUtil::fileExists(outIndexName.c_str()) == true) {
                return outIndexName;
            }
        }
    }

    return "";
}


