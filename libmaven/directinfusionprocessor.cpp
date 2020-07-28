#include "directinfusionprocessor.h"
#include "lipidsummarizationutils.h"

#include <chrono>

using namespace std;
using namespace mzUtils;

shared_ptr<DirectInfusionSearchSet> DirectInfusionProcessor::getSearchSet(mzSample* sample,
                                                                              const vector<Compound*>& compounds,
                                                                              const vector<Adduct*>& adducts,
                                                                              shared_ptr<DirectInfusionSearchParameters> params,
                                                                              bool debug) {

    shared_ptr<DirectInfusionSearchSet> directInfusionSearchSet = shared_ptr<DirectInfusionSearchSet>(new DirectInfusionSearchSet());

    for (Scan* scan : sample->scans){
        if (scan->mslevel == 2){
            int mapKey = static_cast<int>(round(scan->precursorMz+0.001f)); //round to nearest int

            if (directInfusionSearchSet->mzRangesByMapKey.find(mapKey) == directInfusionSearchSet->mzRangesByMapKey.end()) {
                float precMzMin = scan->getPrecMzMin();
                float precMzMax = scan->getPrecMzMax();

                directInfusionSearchSet->mzRangesByMapKey.insert(make_pair(mapKey, make_pair(precMzMin, precMzMax)));
            }

            directInfusionSearchSet->mapKeys.insert(mapKey);
        }
    }

    typedef map<int, pair<float, float>>::iterator mzRangeIterator;

    MassCalculator massCalc;

    if (debug) cerr << "Organizing database into map for fast lookup..." << endl;

    for (Compound *compound : compounds) {
        for (Adduct *adduct : adducts) {

            if (SIGN(adduct->charge) != SIGN(compound->charge)) {
                continue;
            }

            float compoundMz = compound->precursorMz;

            if (params->ms1IsRequireAdductPrecursorMatch){

                if (compound->adductString != adduct->name){
                    continue;
                }

                //TODO: this code works for compounds that do not have an 'adductString' set, but the adduct in the name.
                //delete this eventually.
//                if(compound->name.length() < adduct->name.length() ||
//                   compound->name.compare (compound->name.length() - adduct->name.length(), adduct->name.length(), adduct->name) != 0){
//                    continue;
//                }
            } else {
                compoundMz = adduct->computeAdductMass(massCalc.computeNeutralMass(compound->getFormula()));
            }

            //determine which map key to associate this compound, adduct with

            for (mzRangeIterator it = directInfusionSearchSet->mzRangesByMapKey.begin(); it != directInfusionSearchSet->mzRangesByMapKey.end(); ++it) {
                int mapKey = it->first;
                pair<float, float> mzRange = it->second;

                if (compoundMz > mzRange.first && compoundMz < mzRange.second) {

                    if (directInfusionSearchSet->compoundsByMapKey.find(mapKey) == directInfusionSearchSet->compoundsByMapKey.end()){
                        directInfusionSearchSet->compoundsByMapKey.insert(make_pair(mapKey, vector<pair<Compound*, Adduct*>>()));
                    }

                    directInfusionSearchSet->compoundsByMapKey[mapKey].push_back(make_pair(compound, adduct));
                    break;
                }
            }
        }
    }

    return directInfusionSearchSet;

}

vector<Ms3Compound*> DirectInfusionProcessor::getMs3CompoundSet(const vector<Compound*>& compounds,
                                                                bool debug){
    vector<Ms3Compound*> ms3Compounds(compounds.size());

    for (unsigned int i = 0; i < compounds.size(); i++) {
        ms3Compounds[i] = new Ms3Compound(compounds[i]); //WARNING: this delete this at some point to avoid memory leak
    }

    if (debug) cout << "Created database of " << ms3Compounds.size() << " Ms3Compounds." << endl;

    return ms3Compounds;
}

vector<Ms3SingleSampleMatch*> DirectInfusionProcessor::processSingleMs3Sample(mzSample* sample,
                                                                                  const vector<Ms3Compound*>& ms3Compounds,
                                                                                  shared_ptr<DirectInfusionSearchParameters> params,
                                                                                  bool debug){

    //initialize output
    vector<Ms3SingleSampleMatch*> output;

    map<int, vector<Scan>> ms3ScansByMzPrecursor{};

    vector<pair<double, Scan*>> allMs3Scans;

    vector<Scan*> validMs1Scans;

    for (Scan* scan : sample->scans) {
        if (scan->mslevel == 3) {
            allMs3Scans.push_back(make_pair(scan->precursorMz, scan));
        } else if (scan->mslevel == 1 && scan->filterString.find(params->ms1ScanFilter) != string::npos) {
            validMs1Scans.push_back(scan);
        }
    }

    if (debug) cerr << "Computing consensus MS1 scan..." << endl;

    Fragment *ms1Fragment = nullptr;
    for (auto & scan: validMs1Scans) {
        if (!ms1Fragment) {
            ms1Fragment = new Fragment(scan,
                                       params->scanFilterMinFracIntensity,
                                       params->scanFilterMinSNRatio,
                                       params->scanFilterMaxNumberOfFragments,
                                       params->scanFilterBaseLinePercentile,
                                       params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                                       params->scanFilterPrecursorPurityPpm,
                                       params->scanFilterMinIntensity);
        } else {
            Fragment *ms1Brother = new Fragment(scan,
                                             params->scanFilterMinFracIntensity,
                                             params->scanFilterMinSNRatio,
                                             params->scanFilterMaxNumberOfFragments,
                                             params->scanFilterBaseLinePercentile,
                                             params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                                             params->scanFilterPrecursorPurityPpm,
                                             params->scanFilterMinIntensity);

            ms1Fragment->addFragment(ms1Brother);
        }
    }

    if (ms1Fragment){
        ms1Fragment->buildConsensus(params->consensusMs1PpmTolr,
                                    params->consensusIntensityAgglomerationType,
                                    params->consensusIsIntensityAvgByObserved,
                                    params->consensusIsNormalizeTo10K,
                                    params->consensusMinNumMs1Scans,
                                    params->consensusMinFractionMs1Scans
                                    );

        ms1Fragment->consensus->sortByMz();
    }

    if (debug) cerr << "Finished computing consensus MS1 scan." << endl;

    sort(allMs3Scans.begin(), allMs3Scans.end(), [](const pair<double, Scan*>& lhs, const pair<double, Scan*>& rhs){
        if (lhs.first == rhs.first) {
            return lhs.second->scannum < rhs.second->scannum;
        } else {
            return lhs.first < rhs.first;
        }
    });

    vector<vector<pair<double, Scan*>>> ms3ScanGroups;
    vector<pair<double,Scan*>> scanGroup;
    double lastPrecMz = 0.0;
    unsigned int numProcessedPairs = 0;

    for (unsigned int i = 0; i < allMs3Scans.size(); i++) {

        scanGroup = vector<pair<double,Scan*>>();

        pair<double, Scan*> ithScanPair = allMs3Scans[i];
        lastPrecMz = ithScanPair.first;

        scanGroup.push_back(ithScanPair);

        for (unsigned int j = i+1; j < allMs3Scans.size(); j++) {

            pair<double, Scan*> jthScanPair = allMs3Scans[j];

            if (mzUtils::ppmDist(jthScanPair.first, ithScanPair.first) > static_cast<double>(params->ms3PrecursorPpmTolr)) {
                i = j;
                ms3ScanGroups.push_back(scanGroup);

                numProcessedPairs += scanGroup.size();

                if (debug) cout << "i=" << i << ", numProcessedPairs= " << numProcessedPairs << endl;

                i--; // necessary b/c outer for loop will increment i
                break;
            } else {
                scanGroup.push_back(jthScanPair);
                lastPrecMz = jthScanPair.first;

                if (j == allMs3Scans.size()-1) {
                    i = static_cast<unsigned int>(allMs3Scans.size()); //avoid outer loop
                    break;
                }
            }
        }
    }

    if (!scanGroup.empty()){
        ms3ScanGroups.push_back(scanGroup);
        numProcessedPairs += scanGroup.size();
        if (debug) cout << "i=" << allMs3Scans.size() << ", numProcessedPairs="<< numProcessedPairs << endl;
    }

    //debugging
    if (debug) {

        unsigned int spCounter = 0;
        unsigned int grpCounter = 0;
        for (auto sg : ms3ScanGroups) {

            grpCounter++;
            cout<< "group #" << grpCounter << ": scans=";
            for (auto sp : sg) {
                cout << sp.second->scannum << ", ";
            }
            cout << endl;

            spCounter += sg.size();
        }

        cout << "ms3 scans: # all=" << allMs3Scans.size() << ", # grouped=" << spCounter << endl;
    }

    vector<pair<double, Fragment*>> consensusMs3Spectra(ms3ScanGroups.size());

    for (unsigned int i = 0; i < ms3ScanGroups.size(); i++) {

        auto pairVector = ms3ScanGroups[i];
        Fragment *f = nullptr;
        double avgPrecMz = 0;

        for (auto pair : pairVector) {
            avgPrecMz += pair.first;
            if (!f) {
                f = new Fragment(pair.second,
                                 params->scanFilterMinIntensity,
                                 params->scanFilterMinSNRatio,
                                 params->scanFilterMaxNumberOfFragments,
                                 params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                                 params->scanFilterPrecursorPurityPpm,
                                 params->scanFilterMinIntensity);
            } else {
                Fragment *brother = new Fragment(pair.second,
                                                 params->scanFilterMinIntensity,
                                                 params->scanFilterMinSNRatio,
                                                 params->scanFilterMaxNumberOfFragments,
                                                 params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                                                 params->scanFilterPrecursorPurityPpm,
                                                 params->scanFilterMinIntensity);
                f->addFragment(brother);
            }
        }

        avgPrecMz /= pairVector.size();

        f->buildConsensus(params->consensusMs3PpmTolr,
                          params->consensusIntensityAgglomerationType,
                          params->consensusIsIntensityAvgByObserved,
                          params->consensusIsNormalizeTo10K,
                          params->consensusMinNumMs3Scans,
                          params->consensusMinFractionMs3Scans
                          );

        f->consensus->sortByMz();
        consensusMs3Spectra[i] = make_pair(avgPrecMz, f);
    }

    sort(consensusMs3Spectra.begin(), consensusMs3Spectra.end()); // by default, sorted according to pair.first

    unsigned int compoundCounter = 0;

    for (auto ms3Compound : ms3Compounds) {

        int numMs3Matches = 0;
        map<int, pair<Fragment*, vector<int>>> matchData{};
        string matchInfoDebugString("");

        for (auto it = ms3Compound->ms3_fragment_mzs.begin(); it != ms3Compound->ms3_fragment_mzs.end(); ++it){
            double precMz = mzUtils::intKeyToMz(it->first);

            double minMz = precMz - precMz * params->ms3PrecursorPpmTolr/1000000.0;
            auto lb = lower_bound(consensusMs3Spectra.begin(), consensusMs3Spectra.end(), minMz, [](const pair<double, Fragment*>& lhs, const double& rhs){
                return lhs.first < rhs;
            });

            for (unsigned int pos = lb - consensusMs3Spectra.begin(); pos < consensusMs3Spectra.size(); pos++) {
                pair<double, Fragment*> data = consensusMs3Spectra[pos];
                if (mzUtils::ppmDist(data.first, precMz) <= params->ms3PrecursorPpmTolr) {

                    Fragment t;
                    t.precursorMz = precMz;
                    t.mzs = it->second;
                    t.intensity_array = ms3Compound->ms3_fragment_intensity[it->first];
                    t.fragment_labels = ms3Compound->ms3_fragment_labels[it->first];

                    float maxDeltaMz = (params->ms3PpmTolr * static_cast<float>(t.precursorMz))/ 1000000;
                    vector<int> ranks = Fragment::findFragPairsGreedyMz(&t, data.second->consensus, maxDeltaMz);

                    bool isHasMatch = false;
                    for (unsigned long i = 0; i < ranks.size(); i++) {

                        int y = ranks[i];

                        if (y != -1) {
                            numMs3Matches++;
                            isHasMatch = true;
                            if (debug) {
                                matchInfoDebugString = matchInfoDebugString + "\t"
                                        + t.fragment_labels[i] + " " + to_string(t.mzs[i])
                                        + " <==> "
                                        + to_string(data.second->consensus->mzs[y]) + " (intensity=" + to_string(data.second->consensus->intensity_array[y]) + ")\n";
                            }
                        }
                    }

                    if (isHasMatch) {
                        //                        precMz,           <consensus Fragment*, ranks>
                        matchData.insert(make_pair(it->first, make_pair(data.second, ranks)));
                    }
                }
            } // end ms3Compound m/z map

            //Issue 240: ms1 precursor
            float observedMs1Intensity = 0.0f;

            if (params->ms1IsFindPrecursorIon && ms1Fragment && ms1Fragment->consensus) {
                double precMz = ms3Compound->baseCompound->precursorMz;

                double minMz = precMz - precMz*params->ms1PpmTolr/1e6;
                double maxMz = precMz + precMz*params->ms1PpmTolr/1e6;

                auto lb = lower_bound(ms1Fragment->consensus->mzs.begin(), ms1Fragment->consensus->mzs.end(), minMz);

                auto pos = lb - ms1Fragment->consensus->mzs.begin();

                for (unsigned int i = pos; i < ms1Fragment->consensus->mzs.size(); i++) {
                    if (ms1Fragment->consensus->mzs[i] <= maxMz) {
                        if (ms1Fragment->consensus->intensity_array[i] > observedMs1Intensity) {
                            observedMs1Intensity = ms1Fragment->consensus->intensity_array[i];
                        }
                    } else {
                        break;
                    }
                }
            }

            bool isPassesMs1PrecursorRequirements = !params->ms1IsFindPrecursorIon || (observedMs1Intensity > 0.0f && observedMs1Intensity >= params->ms1MinIntensity);

            if (numMs3Matches >= params->ms3MinNumMatches && isPassesMs1PrecursorRequirements) {

                Ms3SingleSampleMatch *ms3SingleSampleMatch = new Ms3SingleSampleMatch;
                ms3SingleSampleMatch->ms3Compound = ms3Compound;
                ms3SingleSampleMatch->sample = sample;
                ms3SingleSampleMatch->numMs3Matches = numMs3Matches;
                ms3SingleSampleMatch->matchData = matchData;
                ms3SingleSampleMatch->observedMs1Intensity = observedMs1Intensity;

                output.push_back(ms3SingleSampleMatch);
                if (debug) cout << ms3Compound->baseCompound->name << " " << ms3Compound->baseCompound->adductString << ": " << numMs3Matches << " matches; observedMs1Intensity=" << ms3SingleSampleMatch->observedMs1Intensity << endl;
                if (debug) cout << matchInfoDebugString;
            }
        }

        //Issue 244
        if (debug) cout << "Finished comparing compound #" << compoundCounter
                        << " (" << ms3Compound->baseCompound->name << " " << ms3Compound->baseCompound->adductString
                        << "). number of Ms3SingleSampleMatch matches so far: " << output.size() << endl;

        compoundCounter++;
    }

    if (debug){
        long numOutputRows = 0;
        for (auto match : output) {
          numOutputRows += match->numMs3Matches;
        }
        cout << "Identified " << output.size() << " Ms3SingleSampleMatches, with a total of " << numOutputRows << " fragments matched." << endl;
    }

    return output;
}

map<int, DirectInfusionAnnotation*> DirectInfusionProcessor::processSingleSample(mzSample* sample,
                                                                              shared_ptr<DirectInfusionSearchSet> directInfusionSearchSet,
                                                                              shared_ptr<DirectInfusionSearchParameters> params,
                                                                              bool debug) {

    MassCalculator massCalc;
    map<int, DirectInfusionAnnotation*> annotations = {};

    if (debug) cerr << "Started DirectInfusionProcessor::processSingleSample()" << endl;

    //Organize all scans by common precursor m/z

    map<int, vector<Scan*>> ms2ScansByBlockNumber = {};
    vector<Scan*> validMs1Scans;

    for (Scan* scan : sample->scans){
        if (scan->mslevel == 2){
            int mapKey = static_cast<int>(round(scan->precursorMz+0.001f)); //round to nearest int

            if (ms2ScansByBlockNumber.find(mapKey) == ms2ScansByBlockNumber.end()) {
                ms2ScansByBlockNumber.insert(make_pair(mapKey, vector<Scan*>()));
            }

            ms2ScansByBlockNumber[mapKey].push_back(scan);
        }
        if (scan->mslevel == 1 && scan->filterString.find(params->ms1ScanFilter) != string::npos) {
            validMs1Scans.push_back(scan);
        }
    }
    if (debug) cerr << "Performing search over map keys..." << endl;

    //For MS1 quant
    if (debug) cerr << "Computing consensus MS1 scan..." << endl;

    Fragment *ms1Fragment = nullptr;
    for (auto & scan: validMs1Scans) {
        if (!ms1Fragment) {
            ms1Fragment = new Fragment(scan,
                                       params->scanFilterMinFracIntensity,
                                       params->scanFilterMinSNRatio,
                                       params->scanFilterMaxNumberOfFragments,
                                       params->scanFilterBaseLinePercentile,
                                       params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                                       params->scanFilterPrecursorPurityPpm,
                                       params->scanFilterMinIntensity);
        } else {
            Fragment *ms1Brother = new Fragment(scan,
                                             params->scanFilterMinFracIntensity,
                                             params->scanFilterMinSNRatio,
                                             params->scanFilterMaxNumberOfFragments,
                                             params->scanFilterBaseLinePercentile,
                                             params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                                             params->scanFilterPrecursorPurityPpm,
                                             params->scanFilterMinIntensity);

            ms1Fragment->addFragment(ms1Brother);
        }
    }

    if (ms1Fragment){
        ms1Fragment->buildConsensus(params->consensusMs1PpmTolr,
                                    params->consensusIntensityAgglomerationType,
                                    params->consensusIsIntensityAvgByObserved,
                                    params->consensusIsNormalizeTo10K,
                                    params->consensusMinNumMs1Scans,
                                    params->consensusMinFractionMs1Scans
                                    );

        ms1Fragment->consensus->sortByMz();
    }

    if (debug) cerr << "Finished computing consensus MS1 scan." << endl;

    for (auto mapKey : directInfusionSearchSet->mapKeys){

        DirectInfusionAnnotation* directInfusionAnnotation = processBlock(mapKey,
                                                                          directInfusionSearchSet->mzRangesByMapKey[mapKey],
                                                                          sample,
                                                                          ms2ScansByBlockNumber[mapKey],
                                                                          ms1Fragment,
                                                                          directInfusionSearchSet->compoundsByMapKey[mapKey],
                                                                          params,
                                                                          debug);

        if (directInfusionAnnotation) annotations.insert(make_pair(mapKey, directInfusionAnnotation));

    }

    //Issue 232: prevent memory leak
    if (ms1Fragment) delete ms1Fragment;
    ms1Fragment = nullptr;

    return annotations;

}

DirectInfusionAnnotation* DirectInfusionProcessor::processBlock(int blockNum,
                                       const pair<float, float>& mzRange,
                                       mzSample* sample,
                                       const vector<Scan*>& ms2Scans,
                                       const Fragment *ms1Fragment,
                                       const vector<pair<Compound*, Adduct*>> library,
                                       const shared_ptr<DirectInfusionSearchParameters> params,
                                       const bool debug){

    //need MS2 scans and compounds to identify matches
    if (ms2Scans.empty()) return nullptr;
    if (library.empty()) return nullptr;

    //build search spectrum
    Fragment *f = nullptr;
    Scan* representativeScan = nullptr;
    for (auto& scan : ms2Scans) {
        if (!f){
            f = new Fragment(scan,
                             params->scanFilterMinFracIntensity,
                             params->scanFilterMinSNRatio,
                             params->scanFilterMaxNumberOfFragments,
                             params->scanFilterBaseLinePercentile,
                             params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                             params->scanFilterPrecursorPurityPpm,
                             params->scanFilterMinIntensity);
            representativeScan = scan;
        } else {
            Fragment *brother = new Fragment(scan,
                                             params->scanFilterMinFracIntensity,
                                             params->scanFilterMinSNRatio,
                                             params->scanFilterMaxNumberOfFragments,
                                             params->scanFilterBaseLinePercentile,
                                             params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                                             params->scanFilterPrecursorPurityPpm,
                                             params->scanFilterMinIntensity);

            f->addFragment(brother);
        }
    }

    f->buildConsensus(params->consensusPpmTolr,
                      params->consensusIntensityAgglomerationType,
                      params->consensusIsIntensityAvgByObserved,
                      params->consensusIsNormalizeTo10K,
                      params->consensusMinNumMs2Scans,
                      params->consensusMinFractionMs2Scans
                      );

    f->consensus->sortByMz();

    vector<shared_ptr<DirectInfusionMatchData>> libraryMatches;

    //Compare to library
    for (auto libraryEntry : library){

        unique_ptr<DirectInfusionMatchAssessment> matchAssessment = assessMatch(f, ms1Fragment, libraryEntry, params, debug);
        FragmentationMatchScore s = matchAssessment->fragmentationMatchScore;
        float fragmentMaxObservedIntensity = matchAssessment->fragmentMaxObservedIntensity;
        float observedMs1Intensity = matchAssessment->observedMs1Intensity;

        if (s.numMatches >= params->ms2MinNumMatches &&
                s.numDiagnosticMatches >= params->ms2MinNumDiagnosticMatches &&
                params->isDiagnosticFragmentMapAgreement(matchAssessment->diagnosticFragmentMatchMap)) {

            shared_ptr<DirectInfusionMatchData> directInfusionMatchData = shared_ptr<DirectInfusionMatchData>(new DirectInfusionMatchData());

            directInfusionMatchData->compound = libraryEntry.first;
            directInfusionMatchData->adduct = libraryEntry.second;
            directInfusionMatchData->fragmentationMatchScore = s;
            directInfusionMatchData->fragmentMaxObservedIntensity = fragmentMaxObservedIntensity;
            directInfusionMatchData->observedMs1Intensity = observedMs1Intensity;

            libraryMatches.push_back(directInfusionMatchData);
        }
    }

    //agglomerate (if necessary), and return if valid matches exist.
    if (!libraryMatches.empty()){

        //output
        DirectInfusionAnnotation *directInfusionAnnotation = new DirectInfusionAnnotation();
        directInfusionAnnotation->precMzMin = mzRange.first;
        directInfusionAnnotation->precMzMax = mzRange.second;
        directInfusionAnnotation->sample = sample;
        directInfusionAnnotation->scan = representativeScan;
        directInfusionAnnotation->fragmentationPattern = f;

        //determine fragment match maps, and mutate compounds, if needed.
        //includes agglomerating compounds into SummarizedCompounds.
        unique_ptr<DirectInfusionMatchInformation> matchInfo = DirectInfusionProcessor::getMatchInformation(
                    libraryMatches,
                    f->consensus,
                    params,
                    debug);

        DirectInfusionProcessor::addBlockSpecificMatchInfo(matchInfo.get(), f->consensus, params, debug);

        vector<shared_ptr<DirectInfusionMatchData>> processedMatchData{};

        if (params->spectralCompositionAlgorithm == SpectralCompositionAlgorithm::AUTO_SUMMARIZED_MAX_THEORETICAL_INTENSITY_UNIQUE){

            processedMatchData = DirectInfusionProcessor::calculateRankByMaxTheoreticalIntensityOfUniqueFragments(
                        matchInfo.get(),
                        f->consensus,
                        params,
                        debug);
        } else {

            //do not bother to compute rank

            processedMatchData.resize(matchInfo->matchDataToFragsSummarized.size());

            unsigned int i = 0;
            for (auto it = matchInfo->matchDataToFragsSummarized.begin(); it != matchInfo->matchDataToFragsSummarized.end(); ++it){
                processedMatchData[i] = it->first;
                i++;
            }

            directInfusionAnnotation->compounds = processedMatchData;
        }

        //Issue 210: uniqueness filter
        if (params->ms2MinNumUniqueMatches > 0) {
            vector<shared_ptr<DirectInfusionMatchData>> uniqueFilteredMatchData;
            for (auto compound : processedMatchData){
                if (compound->numUniqueFragments >= params->ms2MinNumUniqueMatches) {
                    uniqueFilteredMatchData.push_back(compound);
                }
            }
            processedMatchData = uniqueFilteredMatchData;
        }

        directInfusionAnnotation->compounds = processedMatchData;

        return directInfusionAnnotation;
    }

    return nullptr;
}

unique_ptr<DirectInfusionMatchAssessment> DirectInfusionProcessor::assessMatch(const Fragment *f, //ms2 fragment
                                                             const Fragment *ms1Fragment,
                                                             const pair<Compound*, Adduct*>& libraryEntry,
                                                             const shared_ptr<DirectInfusionSearchParameters> params,
                                                             const bool debug){
    //Initialize output
    unique_ptr<DirectInfusionMatchAssessment> directInfusionMatchAssessment = unique_ptr<DirectInfusionMatchAssessment>(new DirectInfusionMatchAssessment());

    Compound* compound = libraryEntry.first;
    Adduct *adduct = libraryEntry.second;

    //=============================================== //
    //START COMPARE MS1
    //=============================================== //

    float observedMs1Intensity = 0.0f;

    if (ms1Fragment && ms1Fragment->consensus) {
        double precMz = compound->precursorMz;
        if (!params->ms1IsRequireAdductPrecursorMatch) {

            //Compute this way instead of using compound->precursorMz to allow for possibility of matching compound to unexpected adduct
            MassCalculator massCalc;
            float compoundMz = adduct->computeAdductMass(massCalc.computeNeutralMass(compound->getFormula()));
            precMz = adduct->computeAdductMass(compoundMz);

        }

        double minMz = precMz - precMz*params->ms1PpmTolr/1e6;
        double maxMz = precMz + precMz*params->ms1PpmTolr/1e6;

        auto lb = lower_bound(ms1Fragment->consensus->mzs.begin(), ms1Fragment->consensus->mzs.end(), minMz);

        auto pos = lb - ms1Fragment->consensus->mzs.begin();

        for (unsigned int i = pos; i < ms1Fragment->consensus->mzs.size(); i++) {
            if (ms1Fragment->consensus->mzs[i] <= maxMz) {
                if (ms1Fragment->consensus->intensity_array[i] > observedMs1Intensity) {
                    observedMs1Intensity = ms1Fragment->consensus->intensity_array[i];
                }
            } else {
                break;
            }
        }
    }

    bool isPassesMs1PrecursorRequirements = !params->ms1IsFindPrecursorIon || (observedMs1Intensity > 0.0f && observedMs1Intensity >= params->ms1MinIntensity);

    if (!isPassesMs1PrecursorRequirements) return directInfusionMatchAssessment; // will return with no matching fragments, 0 for every score

    //=============================================== //
    //END COMPARE MS1
    //=============================================== //

    //=============================================== //
    //START COMPARE MS2
    //=============================================== //

    Fragment t;
    t.precursorMz = compound->precursorMz;
    t.mzs = compound->fragment_mzs;
    t.intensity_array = compound->fragment_intensity;
    t.fragment_labels = compound->fragment_labels;

    float maxDeltaMz = (params->ms2PpmTolr * static_cast<float>(t.precursorMz))/ 1000000;
    directInfusionMatchAssessment->fragmentationMatchScore.ranks = Fragment::findFragPairsGreedyMz(&t, f->consensus, maxDeltaMz);

    bool isHasLabels = compound->fragment_labels.size() == directInfusionMatchAssessment->fragmentationMatchScore.ranks.size();

    float fragmentMaxObservedIntensity = 0;

    map<string, int> diagnosticMatchesMap = {};
    for (auto it = params->ms2MinNumDiagnosticMatchesMap.begin(); it != params->ms2MinNumDiagnosticMatchesMap.end(); ++it){
        diagnosticMatchesMap.insert(make_pair(it->first, 0));
    }

    for (unsigned long i=0; i < directInfusionMatchAssessment->fragmentationMatchScore.ranks.size(); i++) {

        int y = directInfusionMatchAssessment->fragmentationMatchScore.ranks[i];

        if (y != -1 && f->consensus->intensity_array[y] >= params->ms2MinIntensity) {

            float fragmentObservedIntensity = f->consensus->intensity_array[y];

            if (fragmentObservedIntensity > fragmentMaxObservedIntensity) {
                fragmentMaxObservedIntensity = fragmentObservedIntensity;
            }

            directInfusionMatchAssessment->fragmentationMatchScore.numMatches++;

            if (!isHasLabels) continue;

            if (compound->fragment_labels[i].find("*") == 0) {
                directInfusionMatchAssessment->fragmentationMatchScore.numDiagnosticMatches++;
            }

            for (auto it = params->ms2MinNumDiagnosticMatchesMap.begin(); it != params->ms2MinNumDiagnosticMatchesMap.end(); ++it){
                string diagnosticFragLabel = it->first;
                if (compound->fragment_labels[i].find(diagnosticFragLabel) == 0) {
                    diagnosticMatchesMap[diagnosticFragLabel]++;
                }
            }

        }
    }

    directInfusionMatchAssessment->diagnosticFragmentMatchMap = diagnosticMatchesMap;
    directInfusionMatchAssessment->fragmentMaxObservedIntensity = fragmentMaxObservedIntensity;
    directInfusionMatchAssessment->observedMs1Intensity = observedMs1Intensity;

    //=============================================== //
    //END COMPARE MS2
    //=============================================== //

    return directInfusionMatchAssessment;
}

unique_ptr<DirectInfusionMatchInformation> DirectInfusionProcessor::getFragmentMatchMaps(
        vector<shared_ptr<DirectInfusionMatchData>> allCandidates,
        Fragment *observedSpectrum,
        shared_ptr<DirectInfusionSearchParameters> params,
        bool debug){

       if (debug) cerr << "DirectInfusionProcessor::getFragmentMatchMaps()" << endl;

       unique_ptr<DirectInfusionMatchInformation> matchInfo = unique_ptr<DirectInfusionMatchInformation>(new DirectInfusionMatchInformation());

       for (auto directInfusionMatchData : allCandidates) {

           Compound *compound = directInfusionMatchData->compound;
           FragmentationMatchScore fragmentationMatchScore = directInfusionMatchData->fragmentationMatchScore;

           vector<int> compoundFrags(static_cast<unsigned int>(fragmentationMatchScore.numMatches));

           unsigned int matchCounter = 0;
           for (unsigned int i = 0; i < compound->fragment_mzs.size(); i++) {

               int observedIndex = fragmentationMatchScore.ranks[i];

               //Issue 209: peaks may be unmatched based on intensity as well as ranks[] position
               if (observedIndex == -1 || observedSpectrum->intensity_array[observedIndex] < params->ms2MinIntensity) continue;

               if (debug) cerr << "allCandidates[" << i << "]: " << compound->name << "|" << compound->adductString << " observedIndex=" << observedIndex << endl;

               int fragInt = mzToIntKey(compound->fragment_mzs[i]);

               compoundFrags[matchCounter] = fragInt;
               matchCounter++;

               pair<int, shared_ptr<DirectInfusionMatchData>> key = make_pair(fragInt, directInfusionMatchData);

               matchInfo->fragToTheoreticalIntensity.insert(make_pair(key, (compound->fragment_intensity[i])));

               matchInfo->fragToObservedIntensity.insert(make_pair(key, observedSpectrum->intensity_array[observedIndex]));

               fragToMatchDataIterator it = matchInfo->fragToMatchData.find(fragInt);

               if (it != matchInfo->fragToMatchData.end()) {
                   matchInfo->fragToMatchData[fragInt].push_back(directInfusionMatchData);
               } else {
                   vector<shared_ptr<DirectInfusionMatchData>> matchingCompounds(1);
                   matchingCompounds[0] = directInfusionMatchData;
                   matchInfo->fragToMatchData.insert(make_pair(fragInt, matchingCompounds));
               }

   //            if (debug) cerr << "allCandidates [end] i=" << i << ", ranks=" << fragmentationMatchScore.ranks.size() << endl;
           }

           matchInfo->matchDataToFrags.insert(make_pair(directInfusionMatchData, compoundFrags));

       }

       return matchInfo;

}

/**
 * @brief DirectInfusionProcessor::summarizeByAcylChainsAndSumComposition
 * @param matchInfo
 * @param observedSpectrum
 * @param params
 * @param debug
 * @return
 *
 * @deprecated
 */
unique_ptr<DirectInfusionMatchInformation> DirectInfusionProcessor::summarizeByAcylChainsAndSumComposition(
        unique_ptr<DirectInfusionMatchInformation> matchInfo,
        Fragment *observedSpectrum,
        shared_ptr<DirectInfusionSearchParameters> params,
        bool debug) {

    vector<shared_ptr<DirectInfusionMatchData>> allCandidates(matchInfo->matchDataToFrags.size());
    unsigned int i = 0;
    for (auto it = matchInfo->matchDataToFrags.begin(); it != matchInfo->matchDataToFrags.end(); ++it){
        allCandidates[i] = it->first;
        i++;
    }

    //Identify all cases where matchData matches to identical fragments
    //and compounds can be naturally summarized to a higher level.
    for (unsigned int i = 0; i < allCandidates.size(); i++) {

        shared_ptr<DirectInfusionMatchData> iMatchData = allCandidates[i];
        vector<int> iFrags = matchInfo->matchDataToFrags[iMatchData];

        for (unsigned int j = i+1; j < allCandidates.size(); j++) {

            shared_ptr<DirectInfusionMatchData> jMatchData = allCandidates[j];
            vector<int> jFrags = matchInfo->matchDataToFrags[jMatchData];

            if (iFrags == jFrags && iMatchData->adduct->name == jMatchData->adduct->name) {

                string iChainLengthSummary;
                if (iMatchData->compound->metaDataMap.find(LipidSummarizationUtils::getAcylChainLengthSummaryAttributeKey()) != iMatchData->compound->metaDataMap.end()){
                    iChainLengthSummary = iMatchData->compound->metaDataMap[LipidSummarizationUtils::getAcylChainLengthSummaryAttributeKey()];
                }

                string jChainLengthSummary;
                if (jMatchData->compound->metaDataMap.find(LipidSummarizationUtils::getAcylChainLengthSummaryAttributeKey()) != jMatchData->compound->metaDataMap.end()){
                    jChainLengthSummary = jMatchData->compound->metaDataMap[LipidSummarizationUtils::getAcylChainLengthSummaryAttributeKey()];
                }

                if (!iChainLengthSummary.empty() && !jChainLengthSummary.empty() && iChainLengthSummary == jChainLengthSummary) {
                    if (matchInfo->chainLengthSummaries.find(iChainLengthSummary) != matchInfo->chainLengthSummaries.end()){
                        matchInfo->chainLengthSummaries[iChainLengthSummary].insert(iMatchData);
                        matchInfo->chainLengthSummaries[iChainLengthSummary].insert(jMatchData);
                    } else {
                        set<shared_ptr<DirectInfusionMatchData>> matchDataSet = set<shared_ptr<DirectInfusionMatchData>>();
                        matchDataSet.insert(iMatchData);
                        matchDataSet.insert(jMatchData);
                        matchInfo->chainLengthSummaries.insert(make_pair(iChainLengthSummary, matchDataSet));
                    }

                    matchInfo->originalMatchToSummaryString.insert(make_pair(iMatchData, iChainLengthSummary));
                    matchInfo->originalMatchToSummaryString.insert(make_pair(jMatchData, iChainLengthSummary));
                }

                string iCompositionSummary;
                if (iMatchData->compound->metaDataMap.find(LipidSummarizationUtils::getAcylChainCompositionSummaryAttributeKey()) != iMatchData->compound->metaDataMap.end()) {
                     iCompositionSummary = iMatchData->compound->metaDataMap[LipidSummarizationUtils::getAcylChainCompositionSummaryAttributeKey()];
                }

                string jCompositionSummary;
                if (jMatchData->compound->metaDataMap.find(LipidSummarizationUtils::getAcylChainCompositionSummaryAttributeKey()) != jMatchData->compound->metaDataMap.end()) {
                    jCompositionSummary = jMatchData->compound->metaDataMap[LipidSummarizationUtils::getAcylChainCompositionSummaryAttributeKey()];
                }

                if (!iCompositionSummary.empty() && !jCompositionSummary.empty() && iCompositionSummary == jCompositionSummary) {
                    if (matchInfo->compositionSummaries.find(iCompositionSummary) != matchInfo->compositionSummaries.end()) {
                        matchInfo->compositionSummaries[iCompositionSummary].insert(iMatchData);
                        matchInfo->compositionSummaries[iCompositionSummary].insert(jMatchData);
                    } else {
                        set<shared_ptr<DirectInfusionMatchData>> matchDataSet = set<shared_ptr<DirectInfusionMatchData>>();
                        matchDataSet.insert(iMatchData);
                        matchDataSet.insert(jMatchData);
                        matchInfo->compositionSummaries.insert(make_pair(iCompositionSummary, matchDataSet));
                    }

                    //acyl chain length summarization takes precedence over composition summarization
                    if (matchInfo->originalMatchToSummaryString.find(iMatchData) == matchInfo->originalMatchToSummaryString.end()) {
                        matchInfo->originalMatchToSummaryString.insert(make_pair(iMatchData, iCompositionSummary));
                    }

                    if (matchInfo->originalMatchToSummaryString.find(jMatchData) == matchInfo->originalMatchToSummaryString.end()) {
                        matchInfo->originalMatchToSummaryString.insert(make_pair(jMatchData, iCompositionSummary));
                    }
                }

            }
        }
    }

    //build new candidates list, with summarized candidates (if applicable)
    vector<shared_ptr<DirectInfusionMatchData>> summarizedCandidates;
    set<string> addedSummaries;

    for (auto candidate : allCandidates) {
        if (matchInfo->originalMatchToSummaryString.find(candidate) != matchInfo->originalMatchToSummaryString.end()) {

            string summarizedName = matchInfo->originalMatchToSummaryString[candidate];

            //only add each summarized compound one time.
            if (addedSummaries.find(summarizedName) != addedSummaries.end()) {
                continue;
            }
            addedSummaries.insert(summarizedName);

            vector<Compound*> compounds;

            //check for chain length summary
            if (matchInfo->chainLengthSummaries.find(summarizedName) != matchInfo->chainLengthSummaries.end()) {

                set<shared_ptr<DirectInfusionMatchData>> matches = matchInfo->chainLengthSummaries[summarizedName];
                compounds.resize(matches.size());

                unsigned int counter = 0;
                for (auto match : matches) {
                    compounds[counter] = match->compound;
                    counter++;
                }

            //check for composition summary
            } else if (matchInfo->compositionSummaries.find(summarizedName) != matchInfo->compositionSummaries.end()) {

                set<shared_ptr<DirectInfusionMatchData>> matches = matchInfo->compositionSummaries[summarizedName];
                compounds.resize(matches.size());

                unsigned int counter = 0;
                for (auto match : matches) {
                    compounds[counter] = match->compound;
                    counter++;
                }

            //problem case
            } else {
                cerr << "summarizedName=" << summarizedName << " Did not match to chain or composition summaries." << endl;
                abort();
            }

            //TODO: memory leak (never deleted)
            SummarizedCompound *summarizedCompound = new SummarizedCompound(summarizedName, compounds);

            summarizedCompound->adductString = compounds.at(0)->adductString;
            summarizedCompound->formula = compounds.at(0)->getFormula();
            summarizedCompound->precursorMz = compounds.at(0)->precursorMz;
            summarizedCompound->setExactMass(compounds.at(0)->getExactMass());
            summarizedCompound->charge = compounds.at(0)->charge;
            summarizedCompound->id = summarizedCompound->name + summarizedCompound->adductString;

            summarizedCompound->computeSummarizedData();

            shared_ptr<DirectInfusionMatchData> summarizedMatchData = shared_ptr<DirectInfusionMatchData>(new DirectInfusionMatchData());
            summarizedMatchData->compound = summarizedCompound;
            summarizedMatchData->adduct = candidate->adduct;
            summarizedMatchData->fragmentationMatchScore = summarizedCompound->scoreCompoundHit(observedSpectrum, params->ms2PpmTolr, false);

            summarizedMatchData->observedMs1Intensity = candidate->observedMs1Intensity;

            summarizedCandidates.push_back(summarizedMatchData);

        } else {
            summarizedCandidates.push_back(candidate);
        }
    }

    if (summarizedCandidates.size() == allCandidates.size()) { // no summarization occurred
        matchInfo->fragToMatchDataSummarized = matchInfo->fragToMatchData;
        matchInfo->matchDataToFragsSummarized = matchInfo->matchDataToFrags;
        matchInfo->fragToTheoreticalIntensitySummarized = matchInfo->fragToTheoreticalIntensity;
    } else {

        for (auto directInfusionMatchData : summarizedCandidates) {

            Compound *compound = directInfusionMatchData->compound;
            FragmentationMatchScore fragmentationMatchScore = directInfusionMatchData->fragmentationMatchScore;

            vector<int> compoundFrags(static_cast<unsigned int>(fragmentationMatchScore.numMatches));

            unsigned int matchCounter = 0;
            for (unsigned int i = 0; i < compound->fragment_mzs.size(); i++) {

                int observedIndex = fragmentationMatchScore.ranks[i];

                //Issue 209: peaks may be unmatched based on intensity as well as ranks[] position
                if (observedIndex == -1 || observedSpectrum->intensity_array[observedIndex] < params->ms2MinIntensity) continue;

                if (debug) cerr << "allCandidates[" << i << "]: " << compound->name << "|" << compound->adductString << " observedIndex=" << observedIndex << endl;

                int fragInt = mzToIntKey(compound->fragment_mzs[i]);

                compoundFrags[matchCounter] = fragInt;
                matchCounter++;

                pair<int, shared_ptr<DirectInfusionMatchData>> key = make_pair(fragInt, directInfusionMatchData);

                matchInfo->fragToTheoreticalIntensitySummarized.insert(make_pair(key, (compound->fragment_intensity[i])));

                matchInfo->fragToObservedIntensity.insert(make_pair(key, observedSpectrum->intensity_array[observedIndex]));

                fragToMatchDataIterator it = matchInfo->fragToMatchDataSummarized.find(fragInt);

                if (it != matchInfo->fragToMatchDataSummarized.end()) {
                    matchInfo->fragToMatchDataSummarized[fragInt].push_back(directInfusionMatchData);
                } else {
                    vector<shared_ptr<DirectInfusionMatchData>> matchingCompounds(1);
                    matchingCompounds[0] = directInfusionMatchData;
                    matchInfo->fragToMatchDataSummarized.insert(make_pair(fragInt, matchingCompounds));
                }
            }

            matchInfo->matchDataToFragsSummarized.insert(make_pair(directInfusionMatchData, compoundFrags));

        }
    }

    return matchInfo;
}

/**
 * @brief DirectInfusionProcessor::summarizeByAcylChainsAndSumComposition2
 * @param matchInfo
 * @param observedSpectrum
 * @param params
 * @param debug
 * @return
 *
 * First, organize all compounds based on identical fragment matches.
 *
 * Subdivide each group of identical fragment matches into composition summary groups, acyl summary groups,
 * or none (return original compound).
 *
 * Summarize to the most specific level possible within each group of fragment matches.
 *
 * In order for the summarization to take place, all of the entries have to agree.
 *
 * If even one disagrees, do not try to summarize at that level.
 *
 * If there are no levels left to summarize, revert to the general case (name-based summarization).
 *
 */
unique_ptr<DirectInfusionMatchInformation> DirectInfusionProcessor::summarizeByAcylChainsAndSumComposition2(
        unique_ptr<DirectInfusionMatchInformation> matchInfo,
        Fragment *observedSpectrum,
        shared_ptr<DirectInfusionSearchParameters> params,
        bool debug) {

    //<K, V> = fraglist, compound_info
    map<vector<int>, vector<shared_ptr<DirectInfusionMatchData>>> fragListToCompounds{};

    for (auto it = matchInfo->matchDataToFrags.begin(); it != matchInfo->matchDataToFrags.end(); ++it){
        vector<int> fragList = it->second;
        if (fragListToCompounds.find(fragList) == fragListToCompounds.end()) {
            fragListToCompounds.insert(make_pair(fragList, vector<shared_ptr<DirectInfusionMatchData>>()));
        }
        fragListToCompounds[fragList].push_back(it->first);
    }

    for (auto it = fragListToCompounds.begin(); it != fragListToCompounds.end(); ++it) {

        vector<shared_ptr<DirectInfusionMatchData>> compoundList = it->second;

        //Initialize output
        shared_ptr<DirectInfusionMatchData> summarizedMatchData;

        //Case: if only one match, do not try to do any summarization.
        if (compoundList.size() == 1) {
            summarizedMatchData = compoundList[0];
            matchInfo->matchDataToFragsSummarized.insert(make_pair(summarizedMatchData, it->first));
            continue;
        }

        /*
         *  Determine how to summarize identical fragment matched compounds (acyl chain, composition, or general)
         */

        unordered_set<string> compositionLevel{};
        unordered_set<string> acylChainLevel{};

        bool isMissingCompositionLevel = false;
        bool isMissingAcylChainLevel = false;

        Adduct *adduct = nullptr;
        vector<Compound*> compounds;
        float observedMs1Intensity = 0.0f;

        for (auto matchData : compoundList) {

            compounds.push_back(matchData->compound);
            adduct = matchData->adduct;
            observedMs1Intensity = matchData->observedMs1Intensity;

            if (matchData->compound->metaDataMap.find(LipidSummarizationUtils::getAcylChainCompositionSummaryAttributeKey()) != matchData->compound->metaDataMap.end()){
                compositionLevel.insert(matchData->compound->metaDataMap[LipidSummarizationUtils::getAcylChainCompositionSummaryAttributeKey()]);
            } else {
                isMissingCompositionLevel = true;
            }

            if (matchData->compound->metaDataMap.find(LipidSummarizationUtils::getAcylChainLengthSummaryAttributeKey()) != matchData->compound->metaDataMap.end()){
                acylChainLevel.insert(matchData->compound->metaDataMap[LipidSummarizationUtils::getAcylChainLengthSummaryAttributeKey()]);
            } else {
                isMissingAcylChainLevel = true;
            }
        }

        /*
         * try to summarize to acyl chain level, then composition level, then finally fall back to identical fragment matches.
         */

        bool isUseSummarized = (!isMissingAcylChainLevel && acylChainLevel.size() == 1) || (!isMissingCompositionLevel && compositionLevel.size() == 1);

        if (isUseSummarized) {

            SummarizedCompound *summarizedCompound = nullptr;

            if (!isMissingAcylChainLevel && acylChainLevel.size() == 1) {

                summarizedCompound = new SummarizedCompound(*acylChainLevel.begin(), compounds);

            } else if (!isMissingCompositionLevel && compositionLevel.size() == 1) {

                summarizedCompound = new SummarizedCompound(*compositionLevel.begin(), compounds);

            } else {
                cerr << "Problem in finding appropriate level of summarization for fragment matches!" << endl;
                abort();
            }

            //This is necessary, as the summarized form may actually map to multiple fragments (e.g., multiple TG(60:7)).
            string summarizedId("{" + summarizedCompound->name + summarizedCompound->adductString + "}={");
            for (unsigned int i = 0; i < compoundList.size(); i++) {
                if (i > 0) {
                    summarizedId += ";";
                }
                summarizedId += compoundList[i]->compound->name;
            }
            summarizedId += "}";

            summarizedCompound->adductString = compounds.at(0)->adductString;
            summarizedCompound->formula = compounds.at(0)->getFormula();
            summarizedCompound->precursorMz = compounds.at(0)->precursorMz;
            summarizedCompound->setExactMass(compounds.at(0)->getExactMass());
            summarizedCompound->charge = compounds.at(0)->charge;
            summarizedCompound->id = summarizedId;

            summarizedCompound->computeSummarizedData();

            summarizedMatchData = shared_ptr<DirectInfusionMatchData>(new DirectInfusionMatchData());
            summarizedMatchData->compound = summarizedCompound;
            summarizedMatchData->adduct = adduct;
            summarizedMatchData->fragmentationMatchScore = summarizedCompound->scoreCompoundHit(observedSpectrum, params->ms2PpmTolr, false);

            summarizedMatchData->observedMs1Intensity = observedMs1Intensity;

        } else { //fall back to general summarization based on identical fragments

            string summarizedName("{");

            string adductString("");
            string formulaString("");
            double precursorMz = 0;
            float exactMass = 0;
            int charge = 0;

            map<string, int> adductStringMap{};
            map<string, int> formulaStringMap{};

            vector<Compound*> compoundPtrs(compoundList.size());

            for (unsigned int i = 0; i < compoundList.size(); i++) {
                shared_ptr<DirectInfusionMatchData> compound = compoundList[i];

                compoundPtrs[i] = compound->compound;

                if (i > 0) {
                    summarizedName += ";";
                }

                summarizedName = summarizedName + compound->compound->name + "|" + compound->adduct->name;

                precursorMz += compound->compound->precursorMz;
                exactMass += compound->compound->getExactMass();
                charge += compound->compound->charge;

                if (adductStringMap.find(compound->adduct->name) == adductStringMap.end()){
                    adductStringMap.insert(make_pair(compound->adduct->name, 0));
                }
                adductStringMap[compound->adduct->name]++;

                if (formulaStringMap.find(compound->compound->formula) == formulaStringMap.end()) {
                    formulaStringMap.insert(make_pair(compound->compound->formula, 0));
                }
                formulaStringMap[compound->compound->formula]++;
            }

            precursorMz /= compoundList.size();
            exactMass /= compoundList.size();
            charge = static_cast<int>(charge/compoundList.size());

            vector<pair<string, int>> adductNameCounts{};
            for (auto it = adductStringMap.begin(); it != adductStringMap.end(); ++it){
                adductNameCounts.push_back(make_pair(it->first, it->second));
            }

            vector<pair<string, int>> formulaNameCounts{};
            for (auto it = formulaStringMap.begin(); it != formulaStringMap.end(); ++it){
                formulaNameCounts.push_back(make_pair(it->first, it->second));
            }

            sort(adductNameCounts.begin(), adductNameCounts.end(), [](const pair<string, int>& lhs, const pair<string, int>& rhs){
                if (lhs.second == rhs.second) {
                    return lhs.first.compare(rhs.first);
                } else {
                    return lhs.second - rhs.second;
                }
            });

            sort(formulaNameCounts.begin(), formulaNameCounts.end(), [](const pair<string, int>& lhs, const pair<string, int>& rhs){
                if (lhs.second == rhs.second) {
                    return lhs.first.compare(rhs.first);
                } else {
                    return lhs.second - rhs.second;
                }
            });

            adductString = adductNameCounts[0].first;
            formulaString = formulaNameCounts[0].first;

            Adduct *adduct = nullptr;
            for (auto match : compoundList){
                if (match->adduct->name == adductString) {
                    adduct = match->adduct;
                    break;
                }
            }
            summarizedName += "}";

            SummarizedCompound *summarizedCompound = new SummarizedCompound(summarizedName, compoundPtrs);

            summarizedCompound->adductString = adductString;
            summarizedCompound->formula = formulaString;
            summarizedCompound->precursorMz = precursorMz;
            summarizedCompound->setExactMass(exactMass);
            summarizedCompound->charge = charge;
            summarizedCompound->id = summarizedName + adductString;

            summarizedCompound->computeSummarizedData();

            summarizedMatchData = shared_ptr<DirectInfusionMatchData>(new DirectInfusionMatchData());
            summarizedMatchData->compound = summarizedCompound;
            summarizedMatchData->adduct = adduct;
            summarizedMatchData->fragmentationMatchScore = summarizedCompound->scoreCompoundHit(observedSpectrum, params->ms2PpmTolr, false);

            summarizedMatchData->observedMs1Intensity = observedMs1Intensity;
        }

        matchInfo->matchDataToFragsSummarized.insert(make_pair(summarizedMatchData, it->first));
    }

    for (auto it = matchInfo->matchDataToFragsSummarized.begin(); it != matchInfo->matchDataToFragsSummarized.end(); ++it) {

        vector<int> fragList = it->second;

        for (auto frag : fragList) {
            if (matchInfo->fragToMatchDataSummarized.find(frag) == matchInfo->fragToMatchDataSummarized.end()) {
                matchInfo->fragToMatchDataSummarized.insert(make_pair(frag, vector<shared_ptr<DirectInfusionMatchData>>()));
            }
            matchInfo->fragToMatchDataSummarized[frag].push_back(it->first);
        }
    }

    return matchInfo;

}

unique_ptr<DirectInfusionMatchInformation> DirectInfusionProcessor::summarizeByIdenticalFragmentMatches(
        unique_ptr<DirectInfusionMatchInformation> matchInfo,
        Fragment *observedSpectrum,
        shared_ptr<DirectInfusionSearchParameters> params,
        bool debug){

        //<K, V> = fraglist, compound_info
    map<vector<int>, vector<shared_ptr<DirectInfusionMatchData>>> fragListToCompounds{};

    for (auto it = matchInfo->matchDataToFrags.begin(); it != matchInfo->matchDataToFrags.end(); ++it){
        vector<int> fragList = it->second;
        if (fragListToCompounds.find(fragList) == fragListToCompounds.end()) {
            fragListToCompounds.insert(make_pair(fragList, vector<shared_ptr<DirectInfusionMatchData>>()));
        }
        fragListToCompounds[fragList].push_back(it->first);
    }

    for (auto it = fragListToCompounds.begin(); it != fragListToCompounds.end(); ++it){

        vector<shared_ptr<DirectInfusionMatchData>> compoundList = it->second;

        shared_ptr<DirectInfusionMatchData> summarizedMatchData;

        if (compoundList.size() == 1) {
            summarizedMatchData = compoundList[0];
        } else {
            string summarizedName("{");

            string adductString("");
            string formulaString("");
            double precursorMz = 0;
            float exactMass = 0;
            int charge = 0;

            map<string, int> adductStringMap{};
            map<string, int> formulaStringMap{};

            vector<Compound*> compoundPtrs(compoundList.size());

            float observedMs1Intensity = 0.0f;

            for (unsigned int i = 0; i < compoundList.size(); i++) {
                shared_ptr<DirectInfusionMatchData> compound = compoundList[i];

                compoundPtrs[i] = compound->compound;

                observedMs1Intensity = compound->observedMs1Intensity;

                if (i > 0) {
                    summarizedName += ";";
                }

                summarizedName = summarizedName + compound->compound->name + "|" + compound->adduct->name;

                precursorMz += compound->compound->precursorMz;
                exactMass += compound->compound->getExactMass();
                charge += compound->compound->charge;

                if (adductStringMap.find(compound->adduct->name) == adductStringMap.end()){
                    adductStringMap.insert(make_pair(compound->adduct->name, 0));
                }
                adductStringMap[compound->adduct->name]++;

                if (formulaStringMap.find(compound->compound->formula) == formulaStringMap.end()) {
                    formulaStringMap.insert(make_pair(compound->compound->formula, 0));
                }
                formulaStringMap[compound->compound->formula]++;
            }

            precursorMz /= compoundList.size();
            exactMass /= compoundList.size();
            charge = static_cast<int>(charge/compoundList.size());

            vector<pair<string, int>> adductNameCounts{};
            for (auto it = adductStringMap.begin(); it != adductStringMap.end(); ++it){
                adductNameCounts.push_back(make_pair(it->first, it->second));
            }

            vector<pair<string, int>> formulaNameCounts{};
            for (auto it = formulaStringMap.begin(); it != formulaStringMap.end(); ++it){
                formulaNameCounts.push_back(make_pair(it->first, it->second));
            }

            sort(adductNameCounts.begin(), adductNameCounts.end(), [](const pair<string, int>& lhs, const pair<string, int>& rhs){
                if (lhs.second == rhs.second) {
                    return lhs.first.compare(rhs.first);
                } else {
                    return lhs.second - rhs.second;
                }
            });

            sort(formulaNameCounts.begin(), formulaNameCounts.end(), [](const pair<string, int>& lhs, const pair<string, int>& rhs){
                if (lhs.second == rhs.second) {
                    return lhs.first.compare(rhs.first);
                } else {
                    return lhs.second - rhs.second;
                }
            });

            adductString = adductNameCounts[0].first;
            formulaString = formulaNameCounts[0].first;

            Adduct *adduct = nullptr;
            for (auto match : compoundList){
                if (match->adduct->name == adductString) {
                    adduct = match->adduct;
                    break;
                }
            }
            summarizedName += "}";

            SummarizedCompound *summarizedCompound = new SummarizedCompound(summarizedName, compoundPtrs);

            summarizedCompound->adductString = adductString;
            summarizedCompound->formula = formulaString;
            summarizedCompound->precursorMz = precursorMz;
            summarizedCompound->setExactMass(exactMass);
            summarizedCompound->charge = charge;
            summarizedCompound->id = summarizedName + adductString;

            summarizedCompound->computeSummarizedData();

            summarizedMatchData = shared_ptr<DirectInfusionMatchData>(new DirectInfusionMatchData());
            summarizedMatchData->compound = summarizedCompound;
            summarizedMatchData->adduct = adduct;
            summarizedMatchData->fragmentationMatchScore = summarizedCompound->scoreCompoundHit(observedSpectrum, params->ms2PpmTolr, false);

            summarizedMatchData->observedMs1Intensity = observedMs1Intensity;

        }

        matchInfo->matchDataToFragsSummarized.insert(make_pair(summarizedMatchData, it->first));
    }

    for (auto it = matchInfo->matchDataToFragsSummarized.begin(); it != matchInfo->matchDataToFragsSummarized.end(); ++it) {

        vector<int> fragList = it->second;

        for (auto frag : fragList) {
            if (matchInfo->fragToMatchDataSummarized.find(frag) == matchInfo->fragToMatchDataSummarized.end()) {
                matchInfo->fragToMatchDataSummarized.insert(make_pair(frag, vector<shared_ptr<DirectInfusionMatchData>>()));
            }
            matchInfo->fragToMatchDataSummarized[frag].push_back(it->first);
        }
    }

    return matchInfo;
}

unique_ptr<DirectInfusionMatchInformation> DirectInfusionProcessor::getMatchInformation(
        vector<shared_ptr<DirectInfusionMatchData>> allCandidates,
        Fragment *observedSpectrum,
        shared_ptr<DirectInfusionSearchParameters> params,
        bool debug){

    if (debug) cerr << "DirectInfusionProcessor::getMatchInformation()" << endl;

    unique_ptr<DirectInfusionMatchInformation> matchInfo = getFragmentMatchMaps(allCandidates, observedSpectrum, params, debug);

    if (params->spectralCompositionAlgorithm == SpectralCompositionAlgorithm::ALL_CANDIDATES){

         //summarized compounds are identical to unsummarized compounds
         matchInfo->fragToMatchDataSummarized = matchInfo->fragToMatchData;
         matchInfo->matchDataToFragsSummarized = matchInfo->matchDataToFrags;

    } else if (params->spectralCompositionAlgorithm == SpectralCompositionAlgorithm::AUTO_SUMMARIZED_MAX_THEORETICAL_INTENSITY_UNIQUE ||
               params->spectralCompositionAlgorithm == SpectralCompositionAlgorithm::AUTO_SUMMARIZED_ACYL_CHAINS_SUM_COMPOSITION) {
         matchInfo = summarizeByAcylChainsAndSumComposition2(move(matchInfo), observedSpectrum, params, debug);
    } else if (params->spectralCompositionAlgorithm == SpectralCompositionAlgorithm::AUTO_SUMMARIZED_IDENTICAL_FRAGMENTS) {
        matchInfo = summarizeByIdenticalFragmentMatches(move(matchInfo), observedSpectrum, params, debug);
    }

    if (debug) {
        cerr << "Fragments --> Compounds: (" << matchInfo->matchDataToFrags.size() << " passing compounds)" << endl;

        for (fragToMatchDataIterator iterator = matchInfo->fragToMatchData.begin(); iterator != matchInfo->fragToMatchData.end(); ++iterator) {
            int frag = iterator->first;
            vector<shared_ptr<DirectInfusionMatchData>> compounds = iterator->second;
            cerr<< "frag= " << intKeyToMz(frag) << " m/z : ";
            for (auto matchData : compounds) {
                cerr << matchData->compound->name << "|" << matchData->compound->adductString << " ";
            }
            cerr << endl;
        }

        cerr << "Compounds --> Fragments: (" << matchInfo->fragToMatchData.size() << " matched fragments)" << endl;

        for (matchDataToFragIterator iterator = matchInfo->matchDataToFrags.begin(); iterator != matchInfo->matchDataToFrags.end(); ++iterator) {

            shared_ptr<DirectInfusionMatchData> directInfusionMatchData = iterator->first;
            vector<int> frags = iterator->second;

            cerr << "Compound= " << directInfusionMatchData->compound->name << "|" << directInfusionMatchData->compound->adductString << ": ";
            for (auto frag : frags){
                cerr << intKeyToMz(frag) << " ";
            }
            cerr << endl;
        }

        cerr << "Chain Summaries --> Compounds:" << endl;

        for (stringToMatchDataIterator iterator = matchInfo->chainLengthSummaries.begin(); iterator != matchInfo->chainLengthSummaries.end(); ++iterator){
            string summary = iterator->first;
            set<shared_ptr<DirectInfusionMatchData>> chainLengthMatchDataSet = iterator->second;

            cerr << "Summary= " << summary << ": ";
            for (auto chainMatch : chainLengthMatchDataSet) {
                cerr << chainMatch->compound->name << "|" << chainMatch->compound->adductString << " ";
            }
            cerr << endl;
        }

        cerr << "Composition Summaries --> Compounds:" << endl;

        for (stringToMatchDataIterator iterator = matchInfo->compositionSummaries.begin(); iterator != matchInfo->compositionSummaries.end(); ++iterator){
            string summary = iterator->first;
            set<shared_ptr<DirectInfusionMatchData>> compositionMatchDataSet = iterator->second;

            cerr << "Summary= " << summary << ": ";
            for (auto compMatch : compositionMatchDataSet) {
                cerr << compMatch->compound->name << "|" << compMatch->compound->adductString << " ";
            }
            cerr << endl;
        }

        cerr << "Summarized Fragments --> Summarized Compounds: (" << matchInfo->matchDataToFragsSummarized.size() << " passing compounds)" << endl;

        for (fragToMatchDataIterator iterator = matchInfo->fragToMatchDataSummarized.begin(); iterator != matchInfo->fragToMatchDataSummarized.end(); ++iterator) {
            int frag = iterator->first;
            vector<shared_ptr<DirectInfusionMatchData>> compounds = iterator->second;
            cerr<< "frag= " << intKeyToMz(frag) << " m/z : ";
            for (auto matchData : compounds) {
                cerr << matchData->compound->name << "|" << matchData->compound->adductString << " ";
            }
            cerr << endl;
        }

        cerr << "Summarized Compounds --> Summarized Fragments: (" << matchInfo->fragToMatchDataSummarized.size() << " matched fragments)" << endl;

        for (matchDataToFragIterator iterator = matchInfo->matchDataToFragsSummarized.begin(); iterator != matchInfo->matchDataToFragsSummarized.end(); ++iterator) {

            shared_ptr<DirectInfusionMatchData> directInfusionMatchData = iterator->first;
            vector<int> frags = iterator->second;

            cerr << "Compound= " << directInfusionMatchData->compound->name << "|" << directInfusionMatchData->compound->adductString << ": ";
            for (auto frag : frags){
                cerr << intKeyToMz(frag) << " ";
            }
            cerr << endl;
        }
    }

    return matchInfo;
}

vector<shared_ptr<DirectInfusionMatchData>> DirectInfusionProcessor::calculateRankByMaxTheoreticalIntensityOfUniqueFragments(
        DirectInfusionMatchInformation *matchInfo,
        Fragment *observedSpectrum,
        shared_ptr<DirectInfusionSearchParameters> params,
        bool debug){

    if (debug) {
        cerr << "matchInfo->fragToMatchDataSummarized: " << matchInfo->fragToMatchDataSummarized.size() << " entries." << endl;
        cerr << "matchInfo->matchDataToFragsSummarized: " << matchInfo->matchDataToFragsSummarized.size() << " entries." << endl;
    }

    map<shared_ptr<DirectInfusionMatchData>, vector<shared_ptr<DirectInfusionSinglePeakMatchData>>> compoundToUniqueFragmentIntensities = {};

    for (fragToMatchDataIterator iterator = matchInfo->fragToMatchDataSummarized.begin(); iterator != matchInfo->fragToMatchDataSummarized.end(); ++iterator) {
        if (iterator->second.size() == 1) { // unique fragment

            shared_ptr<DirectInfusionMatchData> compound = iterator->second[0];
            int fragId = iterator->first;

//                if (debug) cerr << "Found unique fragment for " << compound->compound->name << ": fragId=" << fragId << endl;

            shared_ptr<DirectInfusionSinglePeakMatchData> intensityData = matchInfo->getSinglePeakMatchData(fragId, compound);

//                if (debug) cerr << "Retrieved intensityData for " << compound->compound->name << ": fragId=" << fragId  << "." << endl;

            matchDataToFragIntensityIterator it = compoundToUniqueFragmentIntensities.find(compound);
            if (it != compoundToUniqueFragmentIntensities.end()) {
                compoundToUniqueFragmentIntensities[compound].push_back(intensityData);
            } else {
                vector<shared_ptr<DirectInfusionSinglePeakMatchData>> observedIntensities(1);
                observedIntensities[0] = intensityData;
                compoundToUniqueFragmentIntensities.insert(make_pair(compound, observedIntensities));
            }

        }
    }

    map<shared_ptr<DirectInfusionMatchData>, float> results = {};
    float sumIntensity = 0;

    for (matchDataToFragIntensityIterator iterator = compoundToUniqueFragmentIntensities.begin(); iterator != compoundToUniqueFragmentIntensities.end(); ++iterator){

        shared_ptr<DirectInfusionMatchData> directInfusionMatchData = iterator->first;
        vector<shared_ptr<DirectInfusionSinglePeakMatchData>> fragIntensityDataVector = iterator->second;

        float representativeIntensity = 0;
        float maxNormalizedTheoreticalIntensity = 0;
        for (auto intensityData : fragIntensityDataVector) {
            if (intensityData->normalizedTheoreticalIntensity > maxNormalizedTheoreticalIntensity){
                maxNormalizedTheoreticalIntensity = intensityData->normalizedTheoreticalIntensity;
                representativeIntensity = intensityData->getIntensityRatio();
            }
        }

        sumIntensity += representativeIntensity;
        results.insert(make_pair(directInfusionMatchData, representativeIntensity));

        if (debug) {
            cerr << "Compound= " << directInfusionMatchData->compound->name << "|" << directInfusionMatchData->compound->adductString << ": ";
            for (auto frag : fragIntensityDataVector){
                cerr << frag << " ";
            }
            cerr << "median=" << representativeIntensity << endl;
        }

    }

    vector<shared_ptr<DirectInfusionMatchData>> passingMatchData;
    for (matchDataToFloatIterator iterator = results.begin(); iterator != results.end(); ++iterator) {

        shared_ptr<DirectInfusionMatchData> directInfusionMatchData = iterator->first;
        float intensity = iterator->second;

        double proportion = static_cast<double>(intensity/sumIntensity);

        directInfusionMatchData->proportion = proportion;
        passingMatchData.push_back(directInfusionMatchData);

        if (debug) {
            cerr << "Compound= " << directInfusionMatchData->compound->name << "|" << directInfusionMatchData->compound->adductString <<": " << proportion << endl;
        }
    }

    //Issue 210: Retain match data that do not have any unique fragments.
    for (auto it = matchInfo->matchDataToFragsSummarized.begin(); it != matchInfo->matchDataToFragsSummarized.end(); ++it){

        shared_ptr<DirectInfusionMatchData> directInfusionMatchData = it->first;

        if (results.find(directInfusionMatchData) == results.end()) {
            passingMatchData.push_back(directInfusionMatchData);
        }

    }

    return passingMatchData;
}

void DirectInfusionProcessor::addBlockSpecificMatchInfo(
        DirectInfusionMatchInformation *matchInfo,
        Fragment *observedSpectrum,
        shared_ptr<DirectInfusionSearchParameters> params,
        bool debug){

    for (auto it = matchInfo->matchDataToFragsSummarized.begin(); it != matchInfo->matchDataToFragsSummarized.end(); ++it){
        shared_ptr<DirectInfusionMatchData> compound = it->first;
        compound->isFragmentUnique = vector<bool>(compound->compound->fragment_mzs.size(), false);
    }

    for (auto it = matchInfo->fragToMatchDataSummarized.begin(); it != matchInfo->fragToMatchDataSummarized.end(); ++it){

        int fragMzKey = it->first;
        vector<shared_ptr<DirectInfusionMatchData>> compounds = it->second;

        if (compounds.size() == 1) { // unique fragment

            shared_ptr<DirectInfusionMatchData> matchData = compounds[0];
            double fragMzVal = intKeyToMz(fragMzKey) - 0.00001; // possible rounding error in int -> float conversion

            auto lb_it = lower_bound(matchData->compound->fragment_mzs.begin(), matchData->compound->fragment_mzs.end(), fragMzVal);
            long lb = lb_it - matchData->compound->fragment_mzs.begin();

            matchData->isFragmentUnique[lb] = true;
            matchData->numUniqueFragments++;
        }

    }
}

void DirectInfusionGroupAnnotation::clean() {
    for (map<mzSample*, DirectInfusionAnnotation*>::iterator it = annotationBySample.begin(); it != annotationBySample.end(); ++it) {

        if (it->second->fragmentationPattern) delete(it->second->fragmentationPattern);
        for (auto matchData : it->second->compounds) {
             //SummarizedCompounds are created transiently by directinfusionprocessor, Compounds are retrieved from DB.compounds
//            if (SummarizedCompound* sc = dynamic_cast<SummarizedCompound*>(matchData->compound)){
//                delete(sc);
//            }
        }
        if (it->second) delete(it->second);
    }
    annotationBySample.clear();
}

DirectInfusionGroupAnnotation* DirectInfusionGroupAnnotation::createByAverageProportions(vector<DirectInfusionAnnotation*> crossSampleAnnotations, shared_ptr<DirectInfusionSearchParameters> params, bool debug) {

    DirectInfusionGroupAnnotation *directInfusionGroupAnnotation = new DirectInfusionGroupAnnotation();

    directInfusionGroupAnnotation->precMzMin = crossSampleAnnotations.at(0)->precMzMin;
    directInfusionGroupAnnotation->precMzMax = crossSampleAnnotations.at(0)->precMzMax;

    if (debug) {
        cerr << "=========================================" << endl;
        cerr << "Merging peak groups in precMzRange = [" << directInfusionGroupAnnotation->precMzMin << " - " <<  directInfusionGroupAnnotation->precMzMax << "]" << endl;
    }

    Fragment *f = nullptr;

//    map<shared_ptr<DirectInfusionMatchData>, double, DirectInfusionMatchDataCompare> proportionSums = {};
//    map<shared_ptr<DirectInfusionMatchData>, FragmentationMatchScore, DirectInfusionMatchDataCompare> bestFragMatch = {};

    map<shared_ptr<DirectInfusionMatchData>, double, DirectInfusionMatchDataCompareByNames> proportionSums = {};
    map<shared_ptr<DirectInfusionMatchData>, FragmentationMatchScore, DirectInfusionMatchDataCompareByNames> bestFragMatch = {};

    unsigned int compoundInSampleMatchCounter = 0;

    /**
     * If a sample contains no compounds, it should be excluded from calculation for cross-sample adjusted proportions.
     *
     * Individual proportions of compounds within a single sample all sum to 1, except when no compounds are found in the sample,
     * in which case the individual proportions all sum to 0 (and should be excluded from re-calculating cross-sample contributions)
     */
    unsigned int numContributingSamples = 0;

    for (auto directInfusionAnnotation : crossSampleAnnotations){
        directInfusionGroupAnnotation->annotationBySample.insert(
                    make_pair(directInfusionAnnotation->sample,
                              directInfusionAnnotation)
                    );

        //Issue 218
        if (!f){
            f = new Fragment(directInfusionAnnotation->scan,
                             params->scanFilterMinFracIntensity,
                             params->scanFilterMinSNRatio,
                             params->scanFilterMaxNumberOfFragments,
                             params->scanFilterBaseLinePercentile,
                             params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                             params->scanFilterPrecursorPurityPpm,
                             params->scanFilterMinIntensity);
        } else {
            Fragment *brother = new Fragment(directInfusionAnnotation->scan,
                                             params->scanFilterMinFracIntensity,
                                             params->scanFilterMinSNRatio,
                                             params->scanFilterMaxNumberOfFragments,
                                             params->scanFilterBaseLinePercentile,
                                             params->scanFilterIsRetainFragmentsAbovePrecursorMz,
                                             params->scanFilterPrecursorPurityPpm,
                                             params->scanFilterMinIntensity);

            f->addFragment(brother);
        }

        //Issue 218
        if (directInfusionAnnotation->fragmentationPattern) {
            for (auto fragment : directInfusionAnnotation->fragmentationPattern->brothers) {
                if (fragment) {
                    Fragment *brother = new Fragment(fragment);
                    f->addFragment(brother);
                }
            }
        }

        if (debug) {
            cerr << "sample=" << directInfusionAnnotation->sample->sampleName
                 << ": " << directInfusionAnnotation->compounds.size() << " compounds."
                 << endl;
        }

        if (directInfusionAnnotation->compounds.size() > 0) {
            numContributingSamples++;
        }

        for (auto matchData : directInfusionAnnotation->compounds){

            compoundInSampleMatchCounter++;

            double runningSum = matchData->proportion;

            if (proportionSums.find(matchData) != proportionSums.end()){
                runningSum += proportionSums.at(matchData);
            } else {
                proportionSums.insert(make_pair(matchData, 0.0));
            }

            proportionSums.at(matchData) = runningSum;

            if (debug) {
                cerr << "sample=" << directInfusionAnnotation->sample->sampleName
                     << "(" << matchData->compound->name
                     << ", " << matchData->adduct->name
                     << ", proportion=" << matchData->proportion
                     << "): runningSum=" << runningSum << endl;
            }

            FragmentationMatchScore bestMatch = matchData->fragmentationMatchScore;
            if (bestFragMatch.find(matchData) != bestFragMatch.end()) {
                FragmentationMatchScore previousBestMatch = bestFragMatch.at(matchData);

                //TODO: how to decide on best match?
                if (bestMatch.hypergeomScore >= previousBestMatch.hypergeomScore){
                    bestFragMatch.at(matchData) =  bestMatch;
                }

            } else {
                bestFragMatch.insert(make_pair(matchData, bestMatch));
            }

        }

    }

    if (debug) {
        cerr << "Identified " << proportionSums.size() << " unique and " << compoundInSampleMatchCounter << " total compound-adduct pairs in all samples." << endl;
    }

    //Issue 218
    f->buildConsensus(params->consensusPpmTolr,
                      params->consensusIntensityAgglomerationType,
                      params->consensusIsIntensityAvgByObserved,
                      params->consensusIsNormalizeTo10K,
                      params->consensusMinNumMs2Scans,
                      params->consensusMinFractionMs2Scans
                      );
    f->consensus->sortByMz();

    directInfusionGroupAnnotation->fragmentationPattern = f;

    directInfusionGroupAnnotation->compounds.resize(proportionSums.size());

    double numSamples = static_cast<double>(directInfusionGroupAnnotation->annotationBySample.size());

    unsigned int annotationMatchIndex = 0;

    for (auto matchDataPair : proportionSums) {
       shared_ptr<DirectInfusionMatchData> groupMatchData = shared_ptr<DirectInfusionMatchData>(new DirectInfusionMatchData());

       shared_ptr<DirectInfusionMatchData> matchData = matchDataPair.first;

       groupMatchData->compound = matchData->compound;
       groupMatchData->adduct = matchData->adduct;
       groupMatchData->proportion = matchDataPair.second / numContributingSamples;
       groupMatchData->fragmentationMatchScore = bestFragMatch.at(matchData);
       groupMatchData->fragmentMaxObservedIntensity = matchData->fragmentMaxObservedIntensity;
       groupMatchData->observedMs1Intensity = matchData->observedMs1Intensity;

       directInfusionGroupAnnotation->compounds.at(annotationMatchIndex) = groupMatchData;

       if (debug) {
           cerr << "Compound: " << groupMatchData->compound->name
                << ", Adduct: "<< groupMatchData->adduct->name
                << ", numMatches: " << groupMatchData->fragmentationMatchScore.numMatches
                << ", Proportion: " << groupMatchData->proportion
                << ", Fragment Max Observed Intensity: " << groupMatchData->fragmentMaxObservedIntensity
                << endl;
       }

       annotationMatchIndex++;
    }

    if (debug) {
        cerr << "Determined cross-sample proportions for " << annotationMatchIndex << " compounds." << endl;
        cerr << "=========================================" << endl;
    }

    return directInfusionGroupAnnotation;
}

