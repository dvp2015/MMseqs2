#include "result2stats.h"

#include "Alignment.h"
#include "AminoAcidLookupTables.h"

#include "Debug.h"
#include "Util.h"
#include "FileUtil.h"
#include "itoa.h"

#include <cstdlib>
#include <cmath>

#ifdef OPENMP
#include <omp.h>
#endif

enum {
    STAT_LINECOUNT,
    STAT_MEAN,
    STAT_SUM,
    STAT_DOOLITTLE,
    STAT_CHARGES,
    STAT_SEQLEN,
    STAT_STRLEN,
    STAT_FIRSTLINE,
    STAT_UNKNOWN
};

int MapStatString(const std::string &str) {
    int stat;
    if (str == "linecount") {
        stat = STAT_LINECOUNT;
    } else if (str == "mean") {
        stat = STAT_MEAN;
    } else if (str == "sum") {
        stat = STAT_SUM;
    } else if (str == "doolittle") {
        stat = STAT_DOOLITTLE;
    } else if (str == "charges") {
        stat = STAT_CHARGES;
    } else if (str == "seqlen") {
        stat = STAT_SEQLEN;
    } else if (str == "strlen") {
        stat = STAT_STRLEN;
    } else if (str == "firstline") {
        stat = STAT_FIRSTLINE;
    } else {
        stat = STAT_UNKNOWN;
    }

    return stat;
}

StatsComputer::StatsComputer(const Parameters &par)
        : stat(MapStatString(par.stat)),
          queryDb(par.db1), queryDbIndex(par.db1Index),
          targetDb(par.db2), targetDbIndex(par.db2Index) {
    resultReader = new DBReader<unsigned int>(par.db3.c_str(), par.db3Index.c_str());
    resultReader->open(DBReader<unsigned int>::LINEAR_ACCCESS);

    statWriter = new DBWriter(par.db4.c_str(), par.db4Index.c_str(), (unsigned int) par.threads, DBWriter::BINARY_MODE);
    statWriter->open();
}

float doolittle(const char*);
float charges(const char*);
size_t seqlen(const char* sequence) {
    const char* p = sequence;
    size_t length = 0;
    while (p != NULL) {
        if ((*p >= 'A' && *p < 'Z') || (*p >= 'a' && *p < 'z') || *p == '*') {
            length++;
        } else {
            break;
        }

        p++;
    }
    return length;
}
std::string firstline(const char*);

int StatsComputer::run() {
    switch (stat) {
        case STAT_LINECOUNT:
            return countNumberOfLines();
        case STAT_MEAN:
            return meanValue();
        case STAT_SUM:
            return sumValue();
        case STAT_DOOLITTLE:
            return sequenceWise<float>(&doolittle);
        case STAT_CHARGES:
            return sequenceWise<float>(&charges);
        case STAT_SEQLEN:
            return sequenceWise<size_t>(&seqlen);
        case STAT_STRLEN:
            return sequenceWise<size_t>(&std::strlen);
        case STAT_FIRSTLINE:
            return sequenceWise<std::string>(&firstline, true);
        //case STAT_COMPOSITION:
        //    return sequenceWise(&statsComputer::composition);
        case STAT_UNKNOWN:
        default:
            Debug(Debug::ERROR) << "Unrecognized statistic: " << stat << "\n";
            EXIT(EXIT_FAILURE);
    }
}

StatsComputer::~StatsComputer() {
    statWriter->close();
    resultReader->close();
    delete statWriter;
    delete resultReader;
}

int StatsComputer::countNumberOfLines() {
#pragma omp parallel for schedule(dynamic, 100)
    for (size_t id = 0; id < resultReader->getSize(); id++) {
        Debug::printProgress(id);
        unsigned int thread_idx = 0;
#ifdef OPENMP
        thread_idx = (unsigned int) omp_get_thread_num();
#endif

        unsigned int lineCount(0);
        std::string lineCountString;

        char *results = resultReader->getData(id);
        while (*results != '\0') {
            if (*results == '\n') {
                lineCount++;
            }
            results++;
        }

        lineCountString = SSTR(lineCount) + "\n";

        statWriter->writeData(lineCountString.c_str(), lineCountString.length(), resultReader->getDbKey(id), thread_idx);
    }
    return 0;
}


int StatsComputer::meanValue() {
#pragma omp parallel
    {
        unsigned int thread_idx = 0;
#ifdef OPENMP
        thread_idx = (unsigned int) omp_get_thread_num();
#endif
#pragma omp for schedule(dynamic, 100)
        for (size_t id = 0; id < resultReader->getSize(); id++) {
            Debug::printProgress(id);
            char *results = resultReader->getData(id);

            double meanVal = 0.0;
            size_t nbSeq = 0;
            while (*results != '\0') {
                char *rest;
                errno = 0;
                double value = strtod(results, &rest);
                if (rest == results || errno != 0) {
                    Debug(Debug::WARNING) << "Invalid value in entry " << id << "!\n";
                    continue;
                }

                meanVal += value;
                nbSeq++;
                results = Util::skipLine(results);
            }

            std::string meanValString = SSTR(meanVal / std::max(static_cast<size_t>(1), nbSeq));
            meanValString.append("\n");
            statWriter->writeData(meanValString.c_str(), meanValString.length(), resultReader->getDbKey(id), thread_idx);
        }
    };
    return 0;
}

int StatsComputer::sumValue() {
#pragma omp parallel
    {
        unsigned int thread_idx = 0;
#ifdef OPENMP
        thread_idx = (unsigned int) omp_get_thread_num();
#endif
        char buffer[1024];
#pragma omp for schedule(dynamic, 10)
        for (size_t id = 0; id < resultReader->getSize(); id++) {
            Debug::printProgress(id);
            char *results = resultReader->getData(id);

            size_t sum = 0;
            while (*results != '\0') {
                char *rest;
                errno = 0;
                size_t value = strtoull(results, &rest, 10);
                if (rest == results || errno != 0) {
                    Debug(Debug::WARNING) << "Invalid value in entry " << id << "!\n";
                    continue;
                }

                sum += value;
                results = Util::skipLine(results);
            }

            std::string meanValString = SSTR(sum) + "\n";
            char *end = Itoa::u64toa_sse2(sum, buffer);
            *(end - 1) = '\n';

            statWriter->writeData(buffer, end - buffer, resultReader->getDbKey(id), thread_idx);
        }
    };
    return 0;
}

float doolittle(const char *seq) {
    static Doolittle doolitle;
    return Util::averageValueOnAminoAcids(doolitle.values, seq);
}


float charges(const char *seq) {
    static Charges charges;
    return Util::averageValueOnAminoAcids(charges.values, seq);
}

std::string firstline(const char *seq) {
    std::stringstream ss(seq);
    std::string line;
    std::getline(ss, line);
    return line;
}

template<typename T>
int StatsComputer::sequenceWise(typename PerSequence<T>::type call, bool onlyResultDb) {
    DBReader<unsigned int> *targetReader = NULL;
    if (!onlyResultDb) {
        targetReader = new DBReader<unsigned int>(targetDb.c_str(), targetDbIndex.c_str());
        targetReader->open(DBReader<unsigned int>::NOSORT);
    }

#pragma omp parallel
    {
        unsigned int thread_idx = 0;
#ifdef OPENMP
        thread_idx = (unsigned int) omp_get_thread_num();
#endif
        char dbKey[255 + 1];
        std::string buffer;
        buffer.reserve(1024);

#pragma omp for schedule(dynamic, 10)
        for (size_t id = 0; id < resultReader->getSize(); id++) {
            Debug::printProgress(id);

            char *results = resultReader->getData(id);
            if (onlyResultDb) {
                T stat = (*call)(results);
                buffer.append(SSTR(stat));
                buffer.append("\n");
            } else {
                // for every hit
                int cnt = 0;
                while (*results != '\0') {
                    Util::parseKey(results, dbKey);
                    char *rest;
                    const unsigned int key = (unsigned int) strtoul(dbKey, &rest, 10);
                    if ((rest != dbKey && *rest != '\0') || errno == ERANGE) {
                        Debug(Debug::WARNING) << "Invalid key in entry " << id << "!\n";
                        continue;
                    }

                    const size_t edgeId = targetReader->getId(key);
                    const char *dbSeqData = targetReader->getData(edgeId);

                    T stat = (*call)(dbSeqData);
                    buffer.append(SSTR(stat));
                    buffer.append("\n");

                    results = Util::skipLine(results);
                    cnt++;
                }
            }
            statWriter->writeData(buffer.c_str(), buffer.length(), resultReader->getDbKey(id), thread_idx);
            buffer.clear();
        }
    };

    if(!onlyResultDb) {
        targetReader->close();
        delete targetReader;
    }

    return 0;
}

int result2stats(int argc, const char **argv, const Command &command) {
    Parameters &par = Parameters::getInstance();
    par.parseParameters(argc, argv, command, 4);

#ifdef OPENMP
    omp_set_num_threads(par.threads);
#endif

    StatsComputer computeStats(par);
    return computeStats.run();
}
