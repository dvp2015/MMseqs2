#ifndef ORF_H
#define ORF_H

#include <vector>
#include <string>
#include "Matcher.h"
#include "DBReader.h"

class Orf
{
public:
    enum Strand {
        STRAND_PLUS = 1,
        STRAND_MINUS = -1
    };

    enum Frame {
        FRAME_1 = (1u << 0),
        FRAME_2 = (1u << 1),
        FRAME_3 = (1u << 2)
    };

    enum StartMode {
        START_TO_STOP = 0,
        ANY_TO_STOP = 1,
        LAST_START_TO_STOP = 2
    };

    struct SequenceLocation {
        unsigned int id;
        size_t from, to;
        bool hasIncompleteStart, hasIncompleteEnd;
        Strand strand;
        SequenceLocation(size_t from, size_t to, bool hasIncompleteStart, bool hasIncompleteEnd, Strand strand ):
                id(0), from(from), to(to),
                hasIncompleteStart(hasIncompleteStart),
                hasIncompleteEnd(hasIncompleteEnd),
                strand(strand){}

        SequenceLocation(){}
    };
    
    Orf(const unsigned int requestedGenCode, bool useAllTableStarts);
    ~Orf();

    bool setSequence(const char* sequence, size_t sequenceLength);

    /// Find all ORFs in both orientations that are at least orfMinLength and at most orfMaxLength long.
    /// Report results as SequenceLocations.
    /// seq must be in iupac.
    /// Do not allow more than max_seq_gap consecutive N-or-gap bases in an ORF
    void findAll(std::vector<SequenceLocation> &result,
                 const size_t minLength = 1,
                 const size_t maxLength = SIZE_MAX,
                 const size_t maxGaps = 30,
                 const unsigned int forwardFrames = FRAME_1 | FRAME_2 | FRAME_3,
                 const unsigned int reverseFrames = FRAME_1 | FRAME_2 | FRAME_3,
                 const unsigned int startMode = 0);

    void findForward(const char *sequence, const size_t sequenceLength,
                     std::vector<Orf::SequenceLocation> &result,
                     const size_t minLength, const size_t maxLength, const size_t maxGaps,
                     const unsigned int frames, const unsigned int startMode, const Strand strand);

    std::pair<const char *, size_t> getSequence(const SequenceLocation &location);

    static Matcher::result_t getFromDatabase(const size_t id, DBReader<unsigned int> & contigsReader, DBReader<unsigned int> & orfHeadersReader);

    static SequenceLocation parseOrfHeader(char *data);

private:
    size_t sequenceLength;
    char* sequence;
    char* reverseComplement;
    size_t bufferSize;

    char* stopCodons;
    size_t stopCodonCount;
    char* startCodons;
    size_t startCodonCount;
};

#endif
