#include "directinfusionprocessor.h"
#include "lipidsummarizationutils.h"

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
                float precMzMin = scan->precursorMz - 0.5f * scan->isolationWindow;
                float precMzMax = scan->precursorMz + 0.5f * scan->isolationWindow;

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

            if (params->isRequireAdductPrecursorMatch){

                if (compound->adductString != adduct->name){
                    continue;
                }

                //TODO: this code works for compounds that do not have an 'adductString' set, but the adduct in the name.
                //delete this eventually.
//                if(compound->name.length() < adduct->name.length() ||
//                   compound->name.compare (compound->name.length() - adduct->name.length(), adduct->name.length(), adduct->name) != 0){
//                    continue;
//                }
            }

            if (SIGN(adduct->charge) != SIGN(compound->charge)) {
                continue;
            }

            float compoundMz = adduct->computeAdductMass(massCalc.computeNeutralMass(compound->getFormula()));

            //determine which map key to associate this compound, adduct with

            for (mzRangeIterator it = directInfusionSearchSet->mzRangesByMapKey.begin(); it != directInfusionSearchSet->mzRangesByMapKey.end(); ++it) {
                int mapKey = it->first;
                pair<float, float> mzRange = it->second;

                if (compoundMz > mzRange.first && compoundMz < mzRange.second) {
                    directInfusionSearchSet->compoundsByMapKey.insert(make_pair(mapKey, make_pair(compound, adduct)));
                    break;
                }
            }
        }
    }

    return directInfusionSearchSet;

}

map<int, DirectInfusionAnnotation*> DirectInfusionProcessor::processSingleSample(mzSample* sample,
                                                                              shared_ptr<DirectInfusionSearchSet> directInfusionSearchSet,
                                                                              shared_ptr<DirectInfusionSearchParameters> params,
                                                                              bool debug) {

    MassCalculator massCalc;
    map<int, DirectInfusionAnnotation*> annotations = {};

    if (debug) cerr << "Started DirectInfusionProcessor::processSingleSample()" << endl;

    //Organize all scans by common precursor m/z

    multimap<int, Scan*> scansByPrecursor = {};

    typedef multimap<int, Scan*>::iterator scanIterator;
    typedef multimap<int, pair<Compound*, Adduct*>>::iterator compoundsIterator;

    for (Scan* scan : sample->scans){
        if (scan->mslevel == 2){
            int mapKey = static_cast<int>(round(scan->precursorMz+0.001f)); //round to nearest int

            scansByPrecursor.insert(make_pair(mapKey, scan));
        }
    }
    if (debug) cerr << "Performing search over map keys..." << endl;

    for (auto mapKey : directInfusionSearchSet->mapKeys){

        pair<float,float> mzRange = directInfusionSearchSet->mzRangesByMapKey.at(mapKey);

        float precMzMin = mzRange.first;
        float precMzMax = mzRange.second;

        DirectInfusionAnnotation *directInfusionAnnotation = new DirectInfusionAnnotation();
        directInfusionAnnotation->precMzMin = precMzMin;
        directInfusionAnnotation->precMzMax = precMzMax;
        directInfusionAnnotation->sample = sample;

        vector<shared_ptr<DirectInfusionMatchData>> dIAnnotatedCompounds;

        if (debug) {
            cerr << "=========================================" << endl;
            cerr << "Investigating precMzRange = [" << precMzMin << " - " << precMzMax << "]" << endl;
        }

        pair<scanIterator, scanIterator> scansAtKey = scansByPrecursor.equal_range(mapKey);

        Fragment *f = nullptr;
        int numScansPerPrecursorMz = 0;
        for (scanIterator it = scansAtKey.first; it != scansAtKey.second; ++it) {
            if (numScansPerPrecursorMz == 0){
                f = new Fragment(it->second, 0, 0, UINT_MAX);

                directInfusionAnnotation->scan = it->second;

            } else {
                Fragment *brother = new Fragment(it->second, 0, 0, UINT_MAX);
                f->addFragment(brother);
            }
            numScansPerPrecursorMz++;
        }

        if (!f) {
            delete(directInfusionAnnotation);
            continue;
        }

        f->buildConsensus(params->productPpmTolr); //TODO: a separate parameter?
        f->consensus->sortByMz();

        directInfusionAnnotation->fragmentationPattern = f;

        pair<compoundsIterator, compoundsIterator> compoundMatches = directInfusionSearchSet->compoundsByMapKey.equal_range(mapKey);

        int compCounter = 0;
        int matchCounter = 0;
        for (compoundsIterator it = compoundMatches.first; it != compoundMatches.second; ++it){

            Compound* compound = it->second.first;

            FragmentationMatchScore s = compound->scoreCompoundHit(f->consensus, params->productPpmTolr, false);

            if (s.numMatches >= params->minNumMatches) {
                if (debug) cerr << compound->name << ": " << s.numMatches << endl;

                shared_ptr<DirectInfusionMatchData> directInfusionMatchData = shared_ptr<DirectInfusionMatchData>(new DirectInfusionMatchData());
                directInfusionMatchData->compound = compound;
                directInfusionMatchData->adduct = it->second.second;
                directInfusionMatchData->fragmentationMatchScore = s;

                dIAnnotatedCompounds.push_back(directInfusionMatchData);

                matchCounter++;
            }

            compCounter++;
        }

        if (debug) {
            cerr << "Matched " << matchCounter << "/" << compCounter << " compounds." << endl;
            cerr << "=========================================" << endl;
        }

        if (matchCounter != 0){
            if (params->spectralCompositionAlgorithm == SpectralCompositionAlgorithm::ALL_CANDIDATES) {
                directInfusionAnnotation->compounds = dIAnnotatedCompounds;
            } else {
                directInfusionAnnotation->compounds = DirectInfusionProcessor::determineComposition(dIAnnotatedCompounds, f->consensus, params->spectralCompositionAlgorithm, debug);
            }

            annotations.insert(make_pair(mapKey, directInfusionAnnotation));
        } else {
            delete(directInfusionAnnotation->fragmentationPattern);
            delete(directInfusionAnnotation);
        }
    }

    if (debug) cerr << "Finished DirectInfusionProcessor::processSingleSample()" << endl;

    return annotations;

}

unique_ptr<DirectInfusionMatchInformation> DirectInfusionProcessor::getMatchInformation(
        vector<shared_ptr<DirectInfusionMatchData>> allCandidates,
        Fragment *observedSpectrum,
        bool debug){

    unique_ptr<DirectInfusionMatchInformation> matchInfo = unique_ptr<DirectInfusionMatchInformation>(new DirectInfusionMatchInformation());

    for (auto directInfusionMatchData : allCandidates) {

        Compound *compound = directInfusionMatchData->compound;
        FragmentationMatchScore fragmentationMatchScore = directInfusionMatchData->fragmentationMatchScore;

        vector<int> compoundFrags(static_cast<unsigned int>(fragmentationMatchScore.numMatches));

        unsigned int matchCounter = 0;
        for (unsigned int i = 0; i < compound->fragment_mzs.size(); i++) {

            //skip unmatched peaks
            if (fragmentationMatchScore.ranks.at(i) == -1) continue;

            int fragInt = mzToIntKey(compound->fragment_mzs.at(i), 1000);

            compoundFrags.at(matchCounter) = fragInt;
            matchCounter++;

            pair<int, shared_ptr<DirectInfusionMatchData>> key = make_pair(fragInt, directInfusionMatchData);

            matchInfo->fragToTheoreticalIntensity.insert(make_pair(key, (compound->fragment_intensity.at(i))));

            int observedIndex = fragmentationMatchScore.ranks.at(i);

            matchInfo->fragToObservedIntensity.insert(make_pair(key, observedSpectrum->intensity_array.at(observedIndex)));

            fragToMatchDataIterator it = matchInfo->fragToMatchData.find(fragInt);

            if (it != matchInfo->fragToMatchData.end()) {
                matchInfo->fragToMatchData[fragInt].push_back(directInfusionMatchData);
            } else {
                vector<shared_ptr<DirectInfusionMatchData>> matchingCompounds(1);
                matchingCompounds.at(0) = directInfusionMatchData;
                matchInfo->fragToMatchData.insert(make_pair(fragInt, matchingCompounds));
            }
        }

        matchInfo->matchDataToFrags.insert(make_pair(directInfusionMatchData, compoundFrags));

    }

    map<string, set<shared_ptr<DirectInfusionMatchData>>> chainLengthSummaries = {};
    map<string, set<shared_ptr<DirectInfusionMatchData>>> compositionSummaries = {};

    //Identify all cases where matchData matches to identical fragments
    for (unsigned int i = 0; i < allCandidates.size(); i++) {

        shared_ptr<DirectInfusionMatchData> iMatchData = allCandidates[i];

        for (unsigned int j = i+1; j < allCandidates.size(); j++) {

            shared_ptr<DirectInfusionMatchData> jMatchData = allCandidates[j];

            vector<int> iFrags = matchInfo->matchDataToFrags[iMatchData];
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
                    if (chainLengthSummaries.find(iChainLengthSummary) != chainLengthSummaries.end()){
                        chainLengthSummaries[iChainLengthSummary].insert(iMatchData);
                        chainLengthSummaries[iChainLengthSummary].insert(jMatchData);
                    } else {
                        set<shared_ptr<DirectInfusionMatchData>> matchDataSet = set<shared_ptr<DirectInfusionMatchData>>();
                        matchDataSet.insert(iMatchData);
                        matchDataSet.insert(jMatchData);
                        chainLengthSummaries.insert(make_pair(iChainLengthSummary, matchDataSet));
                    }
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
                    if (compositionSummaries.find(iCompositionSummary) != compositionSummaries.end()) {
                        compositionSummaries[iCompositionSummary].insert(iMatchData);
                        compositionSummaries[iCompositionSummary].insert(jMatchData);
                    } else {
                        set<shared_ptr<DirectInfusionMatchData>> matchDataSet = set<shared_ptr<DirectInfusionMatchData>>();
                        matchDataSet.insert(iMatchData);
                        matchDataSet.insert(jMatchData);
                        compositionSummaries.insert(make_pair(iCompositionSummary, matchDataSet));
                    }
                }

            }
        }
    }

    if (debug) {
        cerr << "Fragments --> Compounds: (" << matchInfo->matchDataToFrags.size() << " passing compounds)" << endl;

        for (fragToMatchDataIterator iterator = matchInfo->fragToMatchData.begin(); iterator != matchInfo->fragToMatchData.end(); ++iterator) {
            int frag = iterator->first;
            vector<shared_ptr<DirectInfusionMatchData>> compounds = iterator->second;
            cerr<< "frag= " << intKeyToMz(frag, 1000) << " m/z : ";
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
                cerr << intKeyToMz(frag, 1000) << " ";
            }
            cerr << endl;
        }

        cerr << "Chain Summaries --> Compounds:" << endl;

        for (stringToMatchDataIterator iterator = chainLengthSummaries.begin(); iterator != chainLengthSummaries.end(); ++iterator){
            string summary = iterator->first;
            set<shared_ptr<DirectInfusionMatchData>> chainLengthMatchDataSet = iterator->second;

            cerr << "Summary= " << summary << ": ";
            for (auto chainMatch : chainLengthMatchDataSet) {
                cerr << chainMatch->compound->name << "|" << chainMatch->compound->adductString << " ";
            }
            cerr << endl;
        }

        cerr << "Composition Summaries --> Compounds:" << endl;

        for (stringToMatchDataIterator iterator = compositionSummaries.begin(); iterator != compositionSummaries.end(); ++iterator){
            string summary = iterator->first;
            set<shared_ptr<DirectInfusionMatchData>> compositionMatchDataSet = iterator->second;

            cerr << "Summary= " << summary << ": ";
            for (auto compMatch : compositionMatchDataSet) {
                cerr << compMatch->compound->name << "|" << compMatch->compound->adductString << " ";
            }
            cerr << endl;
        }
    }

    return matchInfo;
}

vector<shared_ptr<DirectInfusionMatchData>> DirectInfusionProcessor::determineComposition(
        vector<shared_ptr<DirectInfusionMatchData>> allCandidates,
        Fragment *observedSpectrum,
        enum SpectralCompositionAlgorithm algorithm,
        bool debug){

    unique_ptr<DirectInfusionMatchInformation> matchInfo = DirectInfusionProcessor::getMatchInformation(allCandidates, observedSpectrum, debug);

    //TODO: refactor into class, subclass, etc
    if (algorithm == SpectralCompositionAlgorithm::MAX_THEORETICAL_INTENSITY_UNIQUE) {

        map<shared_ptr<DirectInfusionMatchData>, vector<shared_ptr<DirectInfusionSinglePeakMatchData>>> compoundToUniqueFragmentIntensities = {};

        for (fragToMatchDataIterator iterator = matchInfo->fragToMatchData.begin(); iterator != matchInfo->fragToMatchData.end(); ++iterator) {
            if (iterator->second.size() == 1) { // unique fragment

                shared_ptr<DirectInfusionMatchData> compound = iterator->second.at(0);
                int fragId = iterator->first;

                shared_ptr<DirectInfusionSinglePeakMatchData> intensityData = matchInfo->getSinglePeakMatchData(fragId, compound);

                matchDataToFragIntensityIterator it = compoundToUniqueFragmentIntensities.find(compound);
                if (it != compoundToUniqueFragmentIntensities.end()) {
                    compoundToUniqueFragmentIntensities[compound].push_back(intensityData);
                } else {
                    vector<shared_ptr<DirectInfusionSinglePeakMatchData>> observedIntensities(1);
                    observedIntensities.at(0) = intensityData;
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

        return passingMatchData;

    }

    return allCandidates;
}

void DirectInfusionGroupAnnotation::clean() {
    for (map<mzSample*, DirectInfusionAnnotation*>::iterator it = annotationBySample.begin(); it != annotationBySample.end(); ++it) {

        if (it->second->fragmentationPattern) delete(it->second->fragmentationPattern);
        for (auto matchData : it->second->compounds) {
             //SummarizedCompounds are created transiently by directinfusionprocessor, Compounds are retrieved from DB.compounds
            if (SummarizedCompound* sc = dynamic_cast<SummarizedCompound*>(matchData->compound)){
                delete(sc);
            }
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

    map<shared_ptr<DirectInfusionMatchData>, double, DirectInfusionMatchDataCompare> proportionSums = {};
    map<shared_ptr<DirectInfusionMatchData>, FragmentationMatchScore, DirectInfusionMatchDataCompare> bestFragMatch = {};

    unsigned int compoundInSampleMatchCounter = 0;

    for (auto directInfusionAnnotation : crossSampleAnnotations){
        directInfusionGroupAnnotation->annotationBySample.insert(
                    make_pair(directInfusionAnnotation->sample,
                              directInfusionAnnotation)
                    );
        if (!f){
            f = new Fragment(directInfusionAnnotation->scan, 0, 0, UINT_MAX);
        } else {
            Fragment *brother = new Fragment(directInfusionAnnotation->scan, 0, 0, UINT_MAX);
            f->addFragment(brother);
        }

        if (debug) {
            cerr << "sample=" << directInfusionAnnotation->sample->sampleName
                 << ": " << directInfusionAnnotation->compounds.size() << " compounds."
                 << endl;
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

    f->buildConsensus(params->productPpmTolr);
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
       groupMatchData->proportion = matchDataPair.second / numSamples;
       groupMatchData->fragmentationMatchScore = bestFragMatch.at(matchData);

       directInfusionGroupAnnotation->compounds.at(annotationMatchIndex) = groupMatchData;

       if (debug) {
           cerr << "Compound: " << groupMatchData->compound->name
                << ", Adduct: "<< groupMatchData->adduct->name
                << ", numMatches: " << groupMatchData->fragmentationMatchScore.numMatches
                << ", Proportion: " << groupMatchData->proportion << endl;
       }

       annotationMatchIndex++;
    }

    if (debug) {
        cerr << "Determined cross-sample proportions for " << annotationMatchIndex << " compounds." << endl;
        cerr << "=========================================" << endl;
    }

    return directInfusionGroupAnnotation;
}

