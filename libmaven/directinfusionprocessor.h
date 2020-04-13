#pragma once

#include "mzSample.h"
#include "mzUtils.h"
#include <memory>

class mzSample;
class DirectInfusionAnnotation;
enum class SpectralCompositionAlgorithm;

/**
 * @brief The DirectInfusionSearchSet class
 * Data container class
 */
class DirectInfusionSearchSet {

public:

    /**
    *  set of all DIA ranges represented in experiment.
    */
    set<int> mapKeys = {};

    /**
     * mapping of each DIA range to actual m/z values
     * <first, second> = <minMz, maxMz>
     */
    map<int, pair<float,float>> mzRangesByMapKey = {};

    /**
     * <key, value> = <map_key, valid pair<Compound,Adduct>>
     */
     map<int, vector<pair<Compound*, Adduct*>>> compoundsByMapKey = {};

};

/**
 * @brief The SpectralDeconvolutionAlgorithm enum
 *
 * Short description of different approaches for spectral deconvolution
 * (goal is to determine the relative proportions of different compounds)
 */
enum class SpectralCompositionAlgorithm {
    ALL_CANDIDATES,
    AUTO_SUMMARIZED_MAX_THEORETICAL_INTENSITY_UNIQUE
};

/**
 * @brief The FragmentSpectrumFormationAlgorithm enum
 *
 * Determines how individual raw scans should be transformed into "Fragment" objects
 * (which refer to an MS2 spectrum suitable for comparison to library spectra).
 */
enum class FragmentSpectrumFormationAlgorithm {
    MAVEN_ORIGINAL,
    ONLY_ABSOLUTE_THRESHOLD
};

/**
 * @brief The DirectInfusionSearchParameters class
 *
 * single class to contain all parameters used in direct infusion search analysis.
 */
class DirectInfusionSearchParameters {

public:

    /** =======================
     * SAMPLE DATA MANIPULATION
     * ========================*/

    FragmentSpectrumFormationAlgorithm fragmentSpectrumFormationAlgorithm = FragmentSpectrumFormationAlgorithm::ONLY_ABSOLUTE_THRESHOLD;

    /**
     * @brief minIndividualMs2ScanIntensity
     * minimum intensity required for a single measurement from an MS2 scan
     * to be used to create a single library-comparable fragment ms2 spectrum.
     */
    float minIndividualMs2ScanIntensity = 0;

    /**
     * ISSUE 191 TODO: not yet implemented
     *
     * @brief minNumMs2ScansForConsensus
     * Minimum number of times an (m/z, intensity) fragment peak
     * must be seen with an intensity greater than @param minIndividualMs2ScanIntensity
     * in all MS2 scans to be included in the consensus spectrum.
     */
    int minNumMs2ScansForConsensus = 0;

    /**
     *
     * ISSUE 191 TODO: not yet implemented
     *
     * @brief minFractionMs2ScansForConsensus
     * Mininum proportion of all available scans in which a given (m/z, intensity)
     * fragment peak is observed with an intensity greater than @param minIndividualMs2ScanIntensity
     * in order to include this peak in the consensus spectrum.
     */
    float minFractionMs2ScansForConsensus = 0;

    /**
     *
     * ISSUE 191 TODO: not yet implemented
     *
     * @brief isIntensityAvgByObserved
     * Consensus intensity should be averaged based on either
     * (A) all scans where a fragment m/z could have been found (isIntensityAvgByObserved=false)
     * or
     * (B) all scans where a framgnet m/z was actually found (isIntensityAvgByObserved=true)
     */
    bool isIntensityAvgByObserved = true;

    /**
     *
     * ISSUE 191 TODO: not yet implemented
     *
     * @brief isNormalizeIntensityArray
     * If true, all intensities are scaled so tha the max intensity value is 10000.
     * If false, intensities values are returned as-is.
     * In regular MAVEN, this is always true.
     */
    bool isNormalizeIntensityArray = false;

    /** ===================
     * MS2 - SEARCH RELATED
     * ==================== */

    /**
     * @brief minNumMatches
     * mininum number of matches for a single <Compound*, Adduct*>
     * to match to a spectrum in order to retain this <Compound*, Adduct*>
     * as a component of the observed spectrum
     */
    int minNumMatches = 5;

    /**
     * @brief minNumDiagnosticFragments
     * minimum number of matches for a single <Compound*, Adduct*>
     * to match to a spectrum in order to retain this <Compound*, Adduct*>,
     * among the fragments marked as diagnostic.
     * Traditionally, diagnostic fragments include a label that starts with an asterisk (*)
     */
    int minNumDiagnosticFragments = 0;

    /**
     * @brief minNumUniqueMatches
     * minimum number of matches for a single <Compound*, Adduct*>
     * with unique fragment m/zs, given the universe of all <Compound*, Adduct*>
     * matches searched.
     *
     * Considered after @param minNumMatches? - the idea is that @param minNumMatches is used
     * to find likely IDs, and @param minNumUniqueMatches might be mroe useful for determining
     * relative composition
     */
    int minNumUniqueMatches = 0;

    /** ===================
     * MS1 - SEARCH RELATED
     * ==================== */

    /**
     * @brief isRequireAdductPrecursorMatch
     * The compound's associated adduct must be the adduct in the supplied list of adducts,
     * otherwise the match will be ignored.
     */
    bool isRequireAdductPrecursorMatch = true;

    /**
     * @brief productPpmTolr
     * tolerance value used for matching library fragment m/z s to Scan m/z s
     */
    float productPpmTolr = 20;

    /**
     * @brief productMinIntensity
     * minimum intensity value for a fragment to be considered real
     */
    float productMinIntensity = 0;

    /**
     * @brief isFindPrecursorIonInMS1Scan
     * If true, only retain matches where some MS1 signal value is identified.
     */
    bool isFindPrecursorIonInMS1Scan = false;

    /**
     * @brief parentPpmTolr
     * tolerance value used for matching theoretical ion m/z to an m/z peak in an MS1 scan
     */
    float parentPpmTolr = 5;

    /**
     * @brief parentMinIntensity
     * min intensity value for a parent MS1 peak to be considered real
     */
    float parentMinIntensity = 0;

    /**
     * @brief ms1ScanFilter
     * required substring match of this to Scan.filterString field
     * to consider that Scan an appropriate MS1 scan.
     */
    string ms1ScanFilter = "";

    /** ===================
     * == AGGLOMERATION ===
     * ==================== */

    /**
     * @brief spectralCompositionAlgorithm
     * By default, do nothing, just return all matches, without doing any elimination or quantitation
     * of spectral components.
     */
    SpectralCompositionAlgorithm spectralCompositionAlgorithm = SpectralCompositionAlgorithm::ALL_CANDIDATES;

    /**
     * @brief isAgglomerateAcrossSamples
     * Option to agglomerate direct infusion results across samples.
     */
    bool isAgglomerateAcrossSamples = false;

    void printParams(){
        cout << "fragmentSpectrumFormationAlgorithm? " << (fragmentSpectrumFormationAlgorithm == FragmentSpectrumFormationAlgorithm::MAVEN_ORIGINAL ? "MAVEN_ORIGINAL" : "DI_TAILORED") << endl
             << "minIndividualMs2ScanIntensity? " << minIndividualMs2ScanIntensity << endl
             << "minNumMs2ScansForConsensus? " << minNumMs2ScansForConsensus << endl
             << "minFractionMs2ScansForConsensus? " << minFractionMs2ScansForConsensus << endl
             << "minNumMatches? " << minNumMatches << endl
             << "minNumDiagnosticFragments? " << minNumDiagnosticFragments << endl
             << "minNumUniqueMatches? " << minNumUniqueMatches << endl
             << "isRequireAdductPrecursorMatch? " << (isRequireAdductPrecursorMatch ? "true" : "false") << endl
             << "productPpmTolr? " << productPpmTolr << " ppm" << endl
             << "productMinIntensity? " << productMinIntensity << endl
             << "isFindPrecursorIonInMS1Scan? " << (isFindPrecursorIonInMS1Scan ? "true" : "false") << endl
             << "parentPpmTolr? " << parentPpmTolr << " ppm" << endl
             << "parentMinIntensity? " << parentMinIntensity << endl
             << "ms1ScanFilter? " << ms1ScanFilter << endl
             << "spectralCompositionAlgorithm? " << (spectralCompositionAlgorithm == SpectralCompositionAlgorithm::ALL_CANDIDATES ? "ALL_CANDIDATES" : "OTHER") << endl
             << "isAgglomerateAcrossSamples? " << (isAgglomerateAcrossSamples ? "true" : "false")
             << endl;
    }

};

/**
 * @brief The DirectInfusionMatchData struct
 *
 * A container class for organizing association data
 */
struct DirectInfusionMatchData {
    Compound* compound;
    Adduct* adduct;
    FragmentationMatchScore fragmentationMatchScore;
    double proportion = 0;
};

/**
 * @brief The DirectInfusionMatchDataCompare struct
 *
 * lhs->compound == rhs->compound iff lhs and rhs point to the same data, even if memory addresses are different
 *
 * Special class for comparisons
 */
struct DirectInfusionMatchDataCompare {
    bool operator() (const shared_ptr<DirectInfusionMatchData>& lhs, const shared_ptr<DirectInfusionMatchData>& rhs) const {
        if (lhs->compound == rhs->compound) {
            return lhs->adduct < rhs->adduct;
        } else {
            return lhs->compound < rhs->compound;
        }
    }
};

struct DirectInfusionMatchDataCompareByNames {
    bool operator() (const shared_ptr<DirectInfusionMatchData>& lhs, const shared_ptr<DirectInfusionMatchData>& rhs) const {
        if (lhs->compound && rhs->compound) {
            if (lhs->compound->name == rhs->compound->name) {
                if (lhs->adduct && rhs->adduct) {
                    return lhs->adduct->name < rhs->adduct->name;
                } else {
                    return false;
                }
            } else {
                return lhs->compound->name < rhs->compound->name;
            }
        } else {
            return false;
        }
    }
};

/**
 * @brief The DirectInfusionSinglePeakMatchData struct
 *
 * container for peak match data, used by @link DirectInfusionMatchInformation
 */
struct DirectInfusionSinglePeakMatchData {
    float normalizedTheoreticalIntensity;
    float observedIntensity;
    float getIntensityRatio() {return (observedIntensity / normalizedTheoreticalIntensity); }
};

/**
 * @brief The DirectInfusionMatchInformation structure
 *
 * A structure to organize all fragment matches from all compound, adduct pairs that match to a single
 * direct infusion spectrum.
 *
 */
struct DirectInfusionMatchInformation {

public:

    //observed
    map<pair<int, shared_ptr<DirectInfusionMatchData>>,float> fragToObservedIntensity = {};

    //unsummarized
    map<int, vector<shared_ptr<DirectInfusionMatchData>>> fragToMatchData = {};
    map<shared_ptr<DirectInfusionMatchData>, vector<int>> matchDataToFrags = {};
    map<pair<int, shared_ptr<DirectInfusionMatchData>>,float> fragToTheoreticalIntensity = {};

    /**
      * If
      *
      * 1. multiple DirectInfusionMatchData match to exactly the same fragments,
      * 2. the DirectInfusionMatchData's associated compounds can be readily summarized to a common form,
      * and
      * 3. the DirectInfusionMatchData's matched adducts are of the same type,
      *
      * these DirectInfusionMatchData can be summarized.
      *
      * When these conditions are met, the summary mappings are saved in these maps
      */
    map<shared_ptr<DirectInfusionMatchData>, string> originalMatchToSummaryString = {};
    map<string, set<shared_ptr<DirectInfusionMatchData>>> chainLengthSummaries = {}; //LipidSummarizationUtils::getAcylChainLengthSummaryAttributeKey()
    map<string, set<shared_ptr<DirectInfusionMatchData>>> compositionSummaries = {}; //LipidSummarizationUtils::getAcylChainCompositionSummaryAttributeKey()

    //summarized
    map<int, vector<shared_ptr<DirectInfusionMatchData>>> fragToMatchDataSummarized = {};
    map<shared_ptr<DirectInfusionMatchData>, vector<int>> matchDataToFragsSummarized  = {};
    map<pair<int, shared_ptr<DirectInfusionMatchData>>,float> fragToTheoreticalIntensitySummarized = {};


    float getNormalizedTheoreticalIntensity(int fragId, shared_ptr<DirectInfusionMatchData> matchData){
        pair<int, shared_ptr<DirectInfusionMatchData>> pair = make_pair(fragId, matchData);
        if (fragToTheoreticalIntensitySummarized.find(pair) != fragToTheoreticalIntensitySummarized.end()) {
            return fragToTheoreticalIntensitySummarized.at(pair);
        } else if (fragToTheoreticalIntensity.find(pair) != fragToTheoreticalIntensity.end()) {
            return fragToTheoreticalIntensity.at(pair);
        } else {
            return 0.0f; //TODO: should this throw an error?
        }
    }

    float getObservedIntensity(int fragId, shared_ptr<DirectInfusionMatchData> matchData){
        return fragToObservedIntensity.at(make_pair(fragId, matchData));
    }

    float getIntensityRatio(int fragId, shared_ptr<DirectInfusionMatchData> matchData){
        return (getObservedIntensity(fragId, matchData) / getNormalizedTheoreticalIntensity(fragId, matchData));
    }

    shared_ptr<DirectInfusionSinglePeakMatchData> getSinglePeakMatchData(int fragId, shared_ptr<DirectInfusionMatchData> matchData){

        shared_ptr<DirectInfusionSinglePeakMatchData> directInfusionSinglePeakMatchData = shared_ptr<DirectInfusionSinglePeakMatchData>(new DirectInfusionSinglePeakMatchData());

        directInfusionSinglePeakMatchData->normalizedTheoreticalIntensity = getNormalizedTheoreticalIntensity(fragId, matchData);
        directInfusionSinglePeakMatchData->observedIntensity = getObservedIntensity(fragId, matchData);

        return directInfusionSinglePeakMatchData;
    }
};

/**
 * @brief The DirectInfusionProcessor class
 * All methods should be static - functional programming paradigm
 */
class DirectInfusionProcessor {

public:
    /**
     * @brief getSearchSet
     * @param sample
     * a representative sample - may be anything
     * @param compounds
     * @param adducts
     * @param isRequireAdductPrecursorMatch
     * @param debug
     * @return DirectInfusionSearchSet
     * --> all compound, adducts, organized into m/z bins.
     *
     * This structure can be reused if all samples in an experiment have the same organization.
     */
     static shared_ptr<DirectInfusionSearchSet> getSearchSet(
             mzSample *sample,
             const vector<Compound*>& compounds,
             const vector<Adduct*>& adducts,
             shared_ptr<DirectInfusionSearchParameters> params,
             bool debug);

    /**
     * @brief processSingleSample
     * @param sample
     * @param directInfusionSearchSet
     * @param debug
     * @return
     *
     * Returns DirectInfusionAnnotation assessments for a single sample.
     * TODO: think about how to agglomerate these across samples?
     * What to do when there are different compositions in different sample?
     * eg, sample 1 has 70% A, 20% B, 10% C, and sample 2 and 50% A, 0% B, 0% C, and 50% D?
     *
     * Definitely some choices to be made here
     */
     static map<int, DirectInfusionAnnotation*> processSingleSample(
             mzSample *sample,
             shared_ptr<DirectInfusionSearchSet> directInfusionSearchSet,
             shared_ptr<DirectInfusionSearchParameters> params,
             bool debug);

     /**
      * @brief deconvolveAllShared
      * @param allCandidates
      * @return
      *
      * Triggered by SpectralDeconvolutionAlgorithm::ALL_SHARED
      *
      * Removes compounds with all shared fragments, computes relative abundance based on
      * unshared fragments.
      *
      * Input is the list of all candidates, plus the observed spectrum they all matched to
      */
     static vector<shared_ptr<DirectInfusionMatchData>> determineComposition(
             vector<shared_ptr<DirectInfusionMatchData>> allCandidates,
             Fragment *observedSpectrum,
             shared_ptr<DirectInfusionSearchParameters> params,
             bool debug
             );

     /**
      * @brief getMatches
      * @param allCandidates
      *
      * @return
      * function to return all compound matches organized into maps, either with key as compound
      * or fragment m/z.
      *
      * fragments converted m/z <--> int keys using mzToIntKey(mz, 1000000) and intKeyToMz(intKey, 1000000).
      *
      * Note that this function does no processing, filtering, or analysis - it simply reorganizes
      * the compound match data into maps.
      */
     static unique_ptr<DirectInfusionMatchInformation> getMatchInformation(
             vector<shared_ptr<DirectInfusionMatchData>> allCandidates,
             Fragment *observedSpectrum,
             shared_ptr<DirectInfusionSearchParameters> params,
             bool debug);


     /**
      * Divide up search into smaller, more manageable parts
      * mapKey --> compounds in the database
      *        --> relevant parts of each individual sample to search
      *
      * --> a function to divide the raw file into relevant pieces
      *     relevant pieces are MS2 scans of interest
      *     ie, scansByPrecursorMap
      */
     //static DirectInfusionAnnotation*
     static DirectInfusionAnnotation* processBlock(int blockNum,
                              const pair<float,float>& mzRange,
                              mzSample* sample,
                              const vector<Scan*>& ms2Scans,
                              const vector<Scan*>& ms1Scans,
                              const vector<pair<Compound*, Adduct*>> library,
                              const shared_ptr<DirectInfusionSearchParameters> params,
                              const bool debug);

     /**
      * @brief assessMatch
      * @param ms2Scans
      * @param ms1Scans
      * @param libraryMatch
      * @param params
      * @param debug
      *
      * Designed to be multithreaded, work of comparing / evaluating individual matches
      */
     static FragmentationMatchScore assessMatch(const Fragment *f,
                             const vector<Scan*>& ms1Scans,
                             const pair<Compound*, Adduct*>& libraryMatch,
                             const shared_ptr<DirectInfusionSearchParameters> params,
                             const bool debug);
};

/**
 * @brief The DirectInfusionAnnotation class
 *
 * MS/MS scans from an @param sample are agglomerated,
 * and compared to a compound database to identify matches,
 * and relative abundance of various compounds in those scans.
 *
 * Essential to the annotation are the @param precMzMin and @param precMzMax
 * fields, which describe the m/z range scanned for this annotation.
 */
class DirectInfusionAnnotation {

public:

    /**
     * @brief precMzMin, precMzMax
     * refers to the m/z of precursors.
     */
    float precMzMin;
    float precMzMax;

    /**
     * @brief sample
     * source sample
     */
    mzSample* sample = nullptr;

    /**
     * @brief scan
     * a single representative scan from the sample.
     */
    Scan *scan = nullptr;

    /**
     * @brief fragmentationPattern
     * Agglomeration of multiple DI scans (if they exist),
     * otherwise same data as 'scan'.
     */
    Fragment* fragmentationPattern;

    /**
     * compound, adduct, and estimated proportion of the spectrum
     * associated with the match.
     *
     * FragmentationMatchScores are also provided.
     */
    vector<shared_ptr<DirectInfusionMatchData>> compounds;
};

/**
 * @brief The DirectInfusionGroupAnnotation class
 * group of DirectInfusionAnnotation results across many samples.
 */
class DirectInfusionGroupAnnotation : public DirectInfusionAnnotation {

public:

    /**
     * @brief annotationBySample
     * retain original samples for reference.
     */
    map<mzSample*, DirectInfusionAnnotation*> annotationBySample = {};

    void clean();

    /**
     * @brief createByAverageProportions
     * @param singleSampleAnnotations
     *
     * @return pointer to DirectInfusionGroupAnnotation object.
     * This pointer must be deleted explicitly! Cannot use smart pointers b/c of QMetaType rules.
     */
    static DirectInfusionGroupAnnotation* createByAverageProportions(
            vector<DirectInfusionAnnotation*> singleSampleAnnotations,
            shared_ptr<DirectInfusionSearchParameters> params,
            bool debug);

};

typedef map<int, vector<shared_ptr<DirectInfusionMatchData>>>::iterator fragToMatchDataIterator;
typedef map<shared_ptr<DirectInfusionMatchData>, vector<int>>::iterator matchDataToFragIterator;
typedef map<shared_ptr<DirectInfusionMatchData>, vector<shared_ptr<DirectInfusionSinglePeakMatchData>>>::iterator matchDataToFragIntensityIterator;
typedef map<shared_ptr<DirectInfusionMatchData>, float>::iterator matchDataToFloatIterator;
typedef map<string, std::set<shared_ptr<DirectInfusionMatchData>>>::iterator stringToMatchDataIterator;
typedef map<shared_ptr<DirectInfusionMatchData>, string>::iterator matchToStringIterator;

