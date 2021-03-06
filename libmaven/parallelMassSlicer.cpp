#include "parallelMassSlicer.h"

void ParallelMassSlicer::algorithmA() {
    delete_all(slices);
    slices.clear();
    cache.clear();
    map< string, int> seen;

    for(unsigned int i=0; i < samples.size(); i++) {
        for(unsigned int j=0; j < samples[i]->scans.size(); j++ ) {
            Scan* scan = samples[i]->scans[j];
            if ( scan->filterLine.empty() ) continue;
            if ( seen.count( scan->filterLine ) ) continue;
            mzSlice* s = new mzSlice(scan->filterLine);
            slices.push_back(s);
            seen[ scan->filterLine ]=1;
        }
    }
    cerr << "#algorithmA" << slices.size() << endl;
}

void ParallelMassSlicer::algorithmB(float userPPM, float minIntensity, int rtStep) { 
	delete_all(slices);
	slices.clear();
	cache.clear();

	float rtWindow=2.0;
	this->_precursorPPM=userPPM;
	this->_minIntensity=minIntensity;

	if (samples.size() > 0 and rtStep > 0 ) rtWindow = (samples[0]->getAverageFullScanTime()*rtStep);

    cerr << "#Parallel algorithmB:" << endl;
    cerr << " PPM=" << userPPM << endl;
    cerr << " rtWindow=" << rtWindow << endl;
    cerr << " rtStep=" << rtStep << endl;
    cerr << " minCharge="     << _minCharge << endl;
    cerr << " maxCharge="     << _maxCharge << endl;
    cerr << " minIntensity="  << _minIntensity << endl;
    cerr << " maxIntensity="  << _maxIntensity << endl;
    cerr << " minMz="  << _minMz << endl;
    cerr << " maxMz="  << _maxMz << endl;
    cerr << " minRt="  << _minRt << endl;
    cerr << " maxRt="  << _maxRt << endl;

#ifdef OMP_PARALLEL
     #pragma omp parallel for ordered num_threads(4) schedule(static)  
#endif
	for(unsigned int i=0; i < samples.size(); i++) {
		//if (slices.size() > _maxSlices) break;
		//
        //Scan* lastScan = NULL;
		cerr << "#algorithmB:" << samples[i]->sampleName << endl;

		for(unsigned int j=0; j < samples[i]->scans.size(); j++ ) {
			Scan* scan = samples[i]->scans[j];
			if (scan->mslevel != 1 ) continue;
            if (_maxRt and !isBetweenInclusive(scan->rt,_minRt,_maxRt)) continue;
			float rt = scan->rt;

                vector<int> charges;
                if (_minCharge > 0 or _maxCharge > 0) charges = scan->assignCharges(userPPM);

                for(unsigned int k=0; k < scan->nobs(); k++ ){
                if (_maxMz and !isBetweenInclusive(scan->mz[k],_minMz,_maxMz)) continue;
                if (_maxIntensity and !isBetweenInclusive(scan->intensity[k],_minIntensity,_maxIntensity)) continue;
                if ((_minCharge or _maxCharge) and !isBetweenInclusive(charges[k],_minCharge,_maxCharge)) continue;

				float mz = scan->mz[k];
				float mzmax = mz + mz/1e6*_precursorPPM;
				float mzmin = mz - mz/1e6*_precursorPPM;

               // if(charges.size()) {
                    //cerr << "Scan=" << scan->scannum << " mz=" << mz << " charge=" << charges[k] << endl;
               // }
				mzSlice* Z;
#ifdef OMP_PARALLEL
    #pragma omp critical
#endif
			    Z = sliceExists(mz,rt);
#ifdef OMP_PARALLEL
    #pragma omp end critical
#endif
		
				if (Z) {  //MERGE
					//cerr << "Merged Slice " <<  Z->mzmin << " " << Z->mzmax 
					//<< " " << scan->intensity[k] << "  " << Z->ionCount << endl;

					Z->ionCount = std::max((float) Z->ionCount, (float ) scan->intensity[k]);
					Z->rtmax = std::max((float)Z->rtmax, rt+2*rtWindow);
					Z->rtmin = std::min((float)Z->rtmin, rt-2*rtWindow);
					Z->mzmax = std::max((float)Z->mzmax, mzmax);
					Z->mzmin = std::min((float)Z->mzmin, mzmin);
					//make sure that mz windown doesn't get out of control
					if (Z->mzmin < mz-(mz/1e6*userPPM)) Z->mzmin =  mz-(mz/1e6*userPPM);
					if (Z->mzmax > mz+(mz/1e6*userPPM)) Z->mzmax =  mz+(mz/1e6*userPPM);
					Z->mz =(Z->mzmin+Z->mzmax)/2; Z->rt=(Z->rtmin+Z->rtmax)/2;
					//cerr << Z->mz << " " << Z->mzmin << " " << Z->mzmax << " " 
					//<< ppmDist((float)Z->mzmin,mz) << endl;
				} else { //NEW SLICE
					//				if ( lastScan->hasMz(mz, userPPM) ) {
                    //cerr << "\t" << rt << "  " << mzmin << "  "  << mzmax << endl;
					mzSlice* s = new mzSlice(mzmin,mzmax, rt-2*rtWindow, rt+2*rtWindow);
					s->ionCount = scan->intensity[k];
					s->rt=scan->rt; 
					s->mz=mz;
#ifdef OMP_PARALLEL
    #pragma omp critical
#endif
					addSlice(s);
#ifdef OMP_PARALLEL
    #pragma omp end critical
#endif
				}

                //if ( slices.size() % 10000 == 0) cerr << "ParallelMassSlicer count=" << slices.size() << endl;
			} //every scan m/z
			//lastScan = scan;
		} //every scan
	} //every samples

	cerr << "#algorithmB:  Found=" << slices.size() << " slices" << endl;
	sort(slices.begin(),slices.end(), mzSlice::compIntensity);
}

void ParallelMassSlicer::addSlice(mzSlice* s) {
		slices.push_back(s);
		int mzRange = s->mz*10;
		cache.insert( pair<int,mzSlice*>(mzRange, s));
	}

void ParallelMassSlicer::algorithmC(float ppm, float minIntensity, float rtWindow, int topN=20, int minCharge=1) {
        delete_all(slices);
        slices.clear();
        cache.clear();

        for(unsigned int i=0; i < samples.size(); i++) {
            mzSample* s = samples[i];
            for(unsigned int j=0; j < s->scans.size(); j++) {
                Scan* scan = samples[i]->scans[j];

                if (scan->mslevel != 1 ) continue;
                vector<int>chargeState = scan->assignCharges(ppm);

                vector<int> positions = scan->intensityOrderDesc();
                for(unsigned int k=0; k< positions.size() && k<topN; k++ ) {
                    int pos = positions[k];
                    if(scan->intensity[pos] < minIntensity) continue;
                    if(scan->isMonoisotopicPrecursor(scan->mz[pos],ppm) == false) continue;
                    //if (chargeState[pos] < minCharge )  continue;

                    float rt = scan->rt;
                    float mz = scan->mz[ pos ];
                    float mzmax = mz + mz/1e6*ppm;
                    float mzmin = mz - mz/1e6*ppm;

                    mzSlice* slice = sliceExists(mz,rt);
                    if(!slice ) {
                        mzSlice* s = new mzSlice(mzmin,mzmax, rt-2*rtWindow, rt+2*rtWindow);
                        s->ionCount = scan->intensity[pos];
                        s->rt=scan->rt;
                        s->mz=mz;
                        slices.push_back(s);
                        int mzRange = mz*10;
                        cache.insert( pair<int,mzSlice*>(mzRange, s));
                    } else if ( slice->ionCount < scan->intensity[pos]) {
                            slice->ionCount = scan->intensity[pos];
                            slice->rt = scan->rt;
                            slice->mz = mz;
                    }

                }
            }
        }
        cerr << "#algorithmC" << slices.size() << endl;
    }

void ParallelMassSlicer::algorithmD(float ppm, float rtWindow) {        //features that have ms2 events
        delete_all(slices);
        slices.clear();
        cache.clear();

        for(unsigned int i=0; i < samples.size(); i++) {
            mzSample* s = samples[i];
            for(unsigned int j=0; j < s->scans.size(); j++) {
                Scan* scan = samples[i]->scans[j];
                if (scan->mslevel != 2 ) continue;
                float rt = scan->rt;
                float mz = scan->precursorMz;
                float mzmax = mz + mz/1e6*ppm;
                float mzmin = mz - mz/1e6*ppm;

                if(! sliceExists(mz,rt) ) {
                    mzSlice* s = new mzSlice(mzmin,mzmax, rt-2*rtWindow, rt+2*rtWindow);
                    s->ionCount = scan->totalIntensity();
                    s->rt=scan->rt;
                    s->mz=mz;
                    slices.push_back(s);
                    int mzRange = mz*10;
                    cache.insert( pair<int,mzSlice*>(mzRange, s));
                }
            }
        }
        cerr << "#algorithmD" << slices.size() << endl;
}

void ParallelMassSlicer::algorithmE(float ppm, float rtHalfWindowInMin) {        //features that have ms2 events
        delete_all(slices);
        slices.clear();
        cache.clear();

		vector<mzSlice*> sample_slices;

        for(unsigned int i=0; i < samples.size(); i++) {
            mzSample* s = samples[i];

            for(unsigned int j=0; j < s->scans.size(); j++) {
                Scan* scan = samples[i]->scans[j];
                if (scan->mslevel != 2 ) continue;
                float rt = scan->rt;
                float mz = scan->precursorMz;
                float mzmax = mz + mz/1e6f*ppm;
                float mzmin = mz - mz/1e6f*ppm;

                mzSlice* s = new mzSlice(mzmin,mzmax, rt-rtHalfWindowInMin, rt+rtHalfWindowInMin);
                s->rt=scan->rt;
                s->mz=mz;
                s->deleteFlag = false;
				sample_slices.push_back(s);
            }
		}

        //sort only by m/z, rely on RT tolerance downstream
        sort(sample_slices.begin(), sample_slices.end(), [ ](const mzSlice* lhs, const mzSlice* rhs){
            return lhs->mz < rhs->mz;
        });

        cerr << "#algorithmE number of mz slices before merge: " << sample_slices.size() << endl;

        unsigned int deleteCounter = 0;

        for(unsigned int i=0; i < sample_slices.size(); i++ ) {

			mzSlice* a  = sample_slices[i];

            if (a->deleteFlag) continue; //skip over if already marked

            // cerr << a->mz << "\t" << a->rt << endl;

            for(unsigned int j=i+1; j < sample_slices.size(); j++ ) {

				mzSlice* b  = sample_slices[j];

                //debugging
//                cout << "(" << i << ", " << j << "): "
//                     << "(i=[" << to_string(a->mzmin) << "-" << to_string(a->mzmax)
//                     << ", " << to_string(a->rtmin) << "-" << to_string(a->rtmax) << "]"
//                     << " deleteFlag= " << (a->deleteFlag ? "TRUE" : "FALSE")
//                     << ") (j=[" << to_string(b->mzmin) << "-" << to_string(b->mzmax)
//                     << ", " << to_string(b->rtmin) << "-" << to_string(b->rtmax) << "]"
//                     << " deleteFlag= " << (b->deleteFlag ? "TRUE" : "FALSE")
//                     << ")"
//                     << endl;

                //Once the distance in m/z exceeds user-specified limit, no need to keep comparing for merges.
                //Note that b->mzmin < a->mzmax comparison is necessary
                if (b->mzmin > a->mzmax && ppmDist(a->mzmax, b->mzmin) > ppm) break;

                //skip over mz slices that have already been merged
                if (b->deleteFlag) continue;

                if (ParallelMassSlicer::isOverlapping(a, b)) {

                    //b swallows up a
                    b->rtmin = min(a->rtmin, b->rtmin);
                    b->rtmax = max(a->rtmax, b->rtmax);

                    b->mzmin = min(a->mzmin, b->mzmin);
                    b->mzmax = max(a->mzmax, b->mzmax);

                    b->mz  = (b->mzmax - b->mzmin)/2;
                    b->rt  = (b->rtmax - b->rtmin)/2;

                    //a is marked to be ignored in the future
                    a->deleteFlag = true;
                    deleteCounter++;
				}

                //b now contains all of the information in a, so proceed to next index
                if (a->deleteFlag) break;
			}
		}

        cerr << deleteCounter << " mz slices flagged for exclusion." << endl;

		for (mzSlice* x: sample_slices) { 
			if (!x->deleteFlag) slices.push_back(x); 
		}

        cerr << "#algorithmE number of mz slices after merge: " << slices.size() << endl;
}

bool ParallelMassSlicer::isOverlapping(mzSlice *a, mzSlice *b){

      bool isMzOverlapping = checkOverlap(a->mzmin, a->mzmax, b->mzmin, b->mzmax) > 0.0f;
      bool isRtOverlapping = checkOverlap(a->rtmin, a->rtmax, b->rtmin, b->rtmax) > 0.0f;

    //debugging
//    cout
//         << "a=[" << to_string(a->mzmin) << "-" << to_string(a->mzmax) << ", " << to_string(a->rtmin) << "-" << to_string(a->rtmax) << "]"
//         << " <--> "
//         << "b=[" << to_string(b->mzmin) << "-" << to_string(b->mzmax) << ", " << to_string(b->rtmin) << "-" << to_string(b->rtmax) << "]"
//         << endl;

//    cout
//         << "isMzOverlapping? " << (isMzOverlapping ? "TRUE" : "FALSE") << " "
//         << "isRtOverlapping? " << (isRtOverlapping ? "TRUE" : "FALSE") << " "
//         << "isOverlapping? " << (isMzOverlapping && isRtOverlapping ? "TRUE" : "FALSE")
//         << endl;

    return isMzOverlapping && isRtOverlapping;
}

mzSlice*  ParallelMassSlicer::sliceExists(float mz, float rt) {
	pair< multimap<int, mzSlice*>::iterator,  multimap<int, mzSlice*>::iterator > ppp;
	ppp = cache.equal_range( (int) (mz*10) );
	multimap<int, mzSlice*>::iterator it2 = ppp.first;

	float bestDist=FLT_MAX; 
	mzSlice* best=NULL;

	for( ;it2 != ppp.second; ++it2 ) {
		mzSlice* x = (*it2).second; 
		if (mz >= x->mzmin && mz <= x->mzmax && rt >= x->rtmin && rt <= x->rtmax) {
			float mzc = (x->mzmax - x->mzmin)/2;
			float rtc = (x->rtmax - x->rtmin)/2;
			float d = sqrt((POW2(mz-mzc) + POW2(rt-rtc)));
			if ( d < bestDist ) { best=x; bestDist=d; }
		}
	}
	return best;
}
