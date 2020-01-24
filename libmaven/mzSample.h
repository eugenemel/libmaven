#ifndef MZSAMPLE_H
#define MZSAMPLE_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <map>
#include <sstream>
#include <cstring>
#include  <limits.h>
#include  <float.h>
#include <iomanip>
#include "assert.h"

#include "pugixml.hpp"
#include "base64.h"
#include "statistics.h"
#include "mzUtils.h"
#include "mzPatterns.h"
#include "mzFit.h"
#include "mzMassCalculator.h"
#include "Matrix.h"
#include "Fragment.h"

#ifdef ZLIB
#include <zlib.h>
#endif


#ifdef CDFPARSER
#include "../libcdfread/ms10.h"
#endif

#include "MSReader.h"

#if defined(WIN32) || defined(WIN64)
//define strncasecmp strnicmp
//define isnan(x) ((x) = (x))
#endif /* Def WIN32 or Def WIN64 */

class mzSample;
class Scan;
class Peak;
class PeakGroup;
class mzSlice;
class EIC;
class Compound;
class Adduct;
class mzLink;
class Reaction;
class MassCalculator;
class ChargedSpecies;
class Fragment;
class Isotope;
struct FragmentationMatchScore;

using namespace pugi;
using namespace mzUtils;
using namespace std;

class mzPoint {
	public:
		mzPoint() {x=y=z=0; }
		mzPoint(double mz,double intensity) { x=mz; y=intensity; z=0;}
		mzPoint(double ix,double iy,double iz) { x=ix; y=iy; z=iz; }
		mzPoint& operator=(const mzPoint& b) { x=b.x; y=b.y; z=b.z; return *this;}
		inline double mz() { return x; }
		inline double intensity() { return y; }
		static bool compX(const mzPoint& a, const mzPoint& b ) { return a.x < b.x; }
		static bool compY(const mzPoint& a, const mzPoint& b ) { return a.y < b.y; }
		static bool compZ(const mzPoint& a, const mzPoint& b ) { return a.z < b.z; }
        double x,y,z;
};


class Scan { 
    public:

    Scan(mzSample* sample, int scannum, int mslevel, float rt, float precursorMz, int polarity);
    void deepcopy(Scan* b); //copy constructor

    inline unsigned int nobs() { return mz.size(); }
    inline mzSample* getSample() { return sample; }
    vector<int> findMatchingMzs(float mzmin, float mzmax);
    int findHighestIntensityPos(float mz, float ppm);		//higest intensity pos
    int findClosestHighestIntensityPos(float mz, float amu_tolr);	//highest intensity pos nearest to the cente mz
    bool isMonoisotopicPrecursor(float monoIsotopeMz, float ppm, int charge=1);

    bool hasMz(float mz, float ppm);
    bool isCentroided() { return centroided; }
    bool isProfile()    { return !centroided; }
    inline int getPolarity() { return polarity; }
    void  setPolarity(int x) { polarity = x; }

    double totalIntensity(){ double sum=0; for(unsigned int i=0;i<intensity.size();i++) sum += intensity[i]; return sum; }
    float maxIntensity()  { float max=0; for(unsigned int i=0;i<intensity.size();i++) if(intensity[i] > max) max=intensity[i]; return max; }
    float minMz()  { if(nobs() > 0) return mz[0]; return 0; }
    float maxMz()  { if(nobs() > 0) return mz[nobs()-1]; return 0; }
    float baseMz();

    vector<int> intensityOrderDesc(); //return postion in a scan from higest to lowerst intensity
    vector<pair<float,float> > getTopPeaks(float minFracCutoff,float minSigNoiseRatio,int dropTopX);
    vector<int>assignCharges(float ppmTolr);

    vector<float> chargeSeries(float Mx, unsigned int Zx);
    ChargedSpecies* deconvolute(float mzfocus, float noiseLevel, float ppmMerge, float minSigNoiseRatio, int minDeconvolutionCharge, int maxDeconvolutionCharge, int minDeconvolutionMass, int maxDeconvolutionMass, int minChargedStates );
	string toMGF();

    void  simpleCentroid();
    void  intensityFilter( int minIntensity);
    void  quantileFilter(int minQuantile);
    void  summary();
    void  log10Transform();

	Scan* getLastFullScan(int historySize);
	vector<mzPoint> getIsolatedRegion(float isolationWindowAmu);
	double getPrecursorPurity(float ppm);

    int mslevel;
    bool centroided;
    float rt;
    int   scannum;

    float precursorMz;
	float isolationWindow;
    float precursorIntensity;
    int precursorCharge;
    string activationMethod;
    int precursorScanNum;
    float productMz;
    float collisionEnergy;
    float injectionTime;

    vector <float> intensity;
    vector <float> mz;
    string scanType;
    string filterLine;
    string filterString = "";
    mzSample* sample;

    void addChildScan(Scan* s) { children.push_back(s); }
    vector<Scan*> getAllChildren() { return children; }
    Scan* getFirstChild() { if(children.size() == 0) return 0; else return children[0]; }
    TMT tmtQuant(); 

    static bool compRt(Scan* a, Scan* b ) { return a->rt < b->rt; }
    static bool compPrecursor(Scan* a, Scan* b ) { return a->precursorMz < b->precursorMz; }
    static bool compIntensity(Scan* a, Scan* b ) { return a->totalIntensity() > b->totalIntensity(); }
    bool operator< (const Scan& b) { return rt < b.rt; }  //default comparision operation

    vector<Isotope> getIsotopicPattern(float centerMz, float ppm, int maxZ, int maxIsotopes);

private:
    	vector<Scan*> children;
        int polarity;


};


class mzSlice { 
    public:

    mzSlice(float mzmin, float mzmax, float rtmin, float rtmax) { this->mzmin=mzmin; this->mzmax=mzmax; this->rtmin=rtmin; this->rtmax=rtmax; this->mz=mzmin+(mzmax-mzmin)/2; this->rt=rtmin+(rtmax-rtmin)/2; compound=NULL; adduct=NULL, ionCount=0; compoundVector=vector<Compound*>();}
    mzSlice(float mz, float rt, float ions) {  this->mz=this->mzmin=this->mzmax=mz; this->rt=this->rtmin=this->rtmax=rt; this->ionCount=ions;  compound=NULL; adduct=NULL, ionCount=0; compoundVector=vector<Compound*>();}
    mzSlice(string filterLine) { mzmin=mzmax=rtmin=rtmax=mz=rt=ionCount=0; compound=NULL; adduct=NULL,srmId=filterLine; compoundVector=vector<Compound*>();}
    mzSlice() { mzmin=mzmax=rtmin=rtmax=mz=rt=ionCount=0; compound=NULL; adduct=NULL; compoundVector=vector<Compound*>();}
    mzSlice(const mzSlice& b) { mzmin=b.mzmin; mzmax=b.mzmax; rtmin=b.rtmin; rtmax=b.rtmax; ionCount=b.ionCount; mz=b.mz; rt=b.rt; compound=b.compound; adduct=b.adduct; srmId=b.srmId; compoundVector=b.compoundVector;}
    mzSlice& operator= (const mzSlice& b) { mzmin=b.mzmin; mzmax=b.mzmax; rtmin=b.rtmin; rtmax=b.rtmax; ionCount=b.ionCount; compound=b.compound; adduct=b.adduct; srmId=b.srmId; mz=b.mz; rt=b.rt; compoundVector=b.compoundVector; return *this; }

        float mzmin;
        float mzmax;
        float rtmin;
        float rtmax;
        float mz;
        float rt;
        float ionCount;
        Compound* compound;
        Adduct*   adduct;
        vector<Compound*> compoundVector;
		bool deleteFlag=0;

	string srmId;

	static bool compIntensity(const mzSlice* a, const mzSlice* b ) { return b->ionCount < a->ionCount; }
	static bool compMz(const mzSlice* a, const mzSlice* b ) { return a->mz < b->mz; }
	static bool compRt(const mzSlice* a, const mzSlice* b ) { return a->rt < b->rt; }
	bool operator< (const mzSlice* b) { return mz < b->mz; }
	bool operator< (const mzSlice& b) { return mz < b.mz; }
};

class mzLink { 
		public: 
			mzLink(){ mz1=mz2=0; value1=value2=0.0; data1=data2=NULL; correlation=0;}
			mzLink( int a, int b, string n ) { value1=a; value2=b; note=n; correlation=0; }
			mzLink( float a,float b, string n ) { mz1=a; mz2=b; note=n; correlation=0; }
			~mzLink() {}
            //link between two mzs
			float mz1;          //from 
			float mz2;          //to
			string note;        //note about this link

            //generic placeholders to attach values to links
			float value1;		
			float value2;	

            //generic  placeholders to attach objects to the link
            void* data1;   
            void* data2;
			//
           float correlation;
           static bool compMz(const mzLink& a, const mzLink& b ) { return a.mz1 < b.mz1; }
           static bool compCorrelation(const mzLink& a, const mzLink& b) { return a.correlation > b.correlation; }
       };

class mzSample {
public:
    mzSample();                         			// constructor
    ~mzSample();
    void openStream(const char* filename);	   	// constructor : load from file
    void loadSample(const char* filename);      // constructor: load from file
    void loadSample(const char* filename, bool isCorrectPrecursor);	   	// constructor : load from file
    void loadMsToolsSample(const char* filename);	// constructor : load using MSToolkit library
    void parseMzData(const char*);			// load data from mzData file
    void parseMzCSV(const char*);			// load data from mzCSV file
    void parseMzXML(const char*);			// load data from mzXML file
    void parseMzML(const char*);			// load data from mzML file
    int  parseCDF (char *filename, int is_verbose);     // load netcdf files
    Scan* parseMzXMLScan(const xml_node& scan, int scannum);		// parse individual scan
    Scan* randomAccessMzXMLScan(int seek_pos_start, int seek_pos_end);
    void writeMzCSV(const char*);
    string cleanSampleName(string fileName);

    map<string,string> mzML_cvParams(xml_node node);
    void parseMzMLChromatogromList(xml_node);
    void parseMzMLSpectrumList(xml_node);
    void summary();						   	// print info about sample
    void calculateMzRtRange();                      //compute min and max values for mz and rt
    float getAverageFullScanTime();			// average time difference between scans
    void enumerateSRMScans();			//srm->scan mapping for QQQ


    float correlation(float mz1,  float mz2, float ppm, float rt1, float rt2 ); //correlation in EIC space
    float getNormalizationConstant() { return _normalizationConstant; }
    void  setNormalizationConstant(float x) { _normalizationConstant = x; }

    Scan* getScan(unsigned int scanNum);				//get indexes for a given scan
    Scan* getAverageScan(float rtmin, float rtmax, int mslevel, int polarity, float resolution);

    EIC* getEIC(float,float,float,float,int);	//get eic based on minMz, maxMz, minRt, maxRt,mslevel
    EIC* getEIC(string srmId);	//get eic based on srmId
    EIC* getEIC(float precursorMz, float collisionEnergy, float productMz, float amuQ1, float amuQ2 );
    EIC* getTIC(float,float,int);		//get Total Ion Chromatogram
    EIC* getBIC(float,float,int);		//get Base Peak Chromatogram
    double getMS1PrecursorMass(Scan* ms2scan,float ppm);
    vector<Scan*> getFragmentationEvents(mzSlice* slice);

    deque <Scan*> scans;
    int sampleId;
    string sampleName;
    string fileName;
    bool isSelected;
    bool isBlank;
    float color[4];	                                //sample display color, [r,g,b, alpha]
    float minMz;
    float maxMz;
    float minRt;
    float maxRt;
    float maxIntensity;
    float minIntensity;
    float totalIntensity;
    int   _sampleOrder;                              //sample display order
    bool  _C13Labeled;
    bool  _N15Labeled;
    bool  _S34Labeled; //Feng note: added to track S34 labeling state
    bool  _D2Labeled;  //Feng note: added to track D2 labeling state
    float _normalizationConstant;
    string  _setName;
    map<string,vector<int> >srmScans;		//srm to scan mapping
    map<string,string> instrumentInfo;		//tags associated with this sample


    //saving and restoring retention times
    vector<float>originalRetentionTimes;       			//saved retention times prior to alignment
    vector<double>polynomialAlignmentTransformation;		//parameters for polynomial transform

    //Instrument PPM Calibration
    vector<double> ppmCorrectionCoef;

    void saveOriginalRetentionTimes();
    void restoreOriginalRetentionTimes();       
	void applyPolynomialTransform();

    //class functions
    void addScan(Scan*s);
    inline int getPolarity() { if( scans.size()>0) return scans[0]->getPolarity(); return 0; }
    inline unsigned int   scanCount() { return(scans.size()); }
    inline string getSampleName() { return sampleName; }
    void setSampleOrder(int x) { _sampleOrder=x; }
    inline int	  getSampleOrder() { return _sampleOrder; }
    inline string  getSetName()  { return _setName; }
    void   setSetName(string x) { _setName=x; }
    void   setSampleName(string x) { sampleName=x; }
    void   setSampleId(int id) { sampleId=id; }
    inline int	  getSampleId() { return sampleId; }

    static float getMaxRt(const vector<mzSample*>&samples);
    bool C13Labeled(){ return _C13Labeled; }
    bool N15Labeled(){ return _N15Labeled; }

    static bool compSampleOrder(const mzSample* a, const mzSample* b ) { return a->_sampleOrder < b->_sampleOrder; }
    static bool compSampleName(const mzSample* a, const mzSample* b ) { return a->sampleName < b->sampleName; }
    static mzSlice getMinMaxDimentions(const vector<mzSample*>& samples);


    static void setFilter_minIntensity(int x ) { filter_minIntensity=x; }
    static void setFilter_centroidScans( bool x) { filter_centroidScans=x; }
    static void setFilter_intensityQuantile(int x ) { filter_intensityQuantile=x; }
    static void setFilter_mslevel(int x ) { filter_mslevel=x; }
    static void setFilter_polarity(int x ) { filter_polarity=x; }

    static int getFilter_minIntensity() { return filter_minIntensity; }
    static int getFilter_intensityQuantile() { return filter_intensityQuantile; }
    static int getFilter_centroidScans() { return filter_centroidScans; }
    static int getFilter_mslevel()  { return filter_mslevel; }
    static int getFilter_polarity() { return filter_polarity; }

    vector<float> getIntensityDistribution(int mslevel);

    private:
        static int filter_minIntensity;
        static bool filter_centroidScans;
        static int filter_intensityQuantile;
		static int filter_mslevel;
        static int filter_polarity;
        ifstream   _iostream;

};

class EIC {
	
	public:

    EIC() {
        sample=NULL;
        mzmin=mzmax=rtmin=rtmax=0;
        maxIntensity=totalIntensity=0;
        eic_noNoiseObs=0;
        smootherType=GAUSSIAN;
        baselineSmoothingWindow=5;  //baseline smoothin window
        baselineDropTopX=60;    //fraction of point to remove when computing baseline
        for(unsigned int i=0; i<4;i++) color[i]=0;
    }

		~EIC();


        enum SmootherType { GAUSSIAN=0, AVG=1, SAVGOL=2 };
        vector <int> scannum;
		vector <float> rt;
		vector <float> mz;
		vector <float> intensity;
		vector <Peak>  peaks;
		string sampleName;

        mzSample* sample;       //pointer to originating sample
        float color[4];         //color of the eic line, [r,g,b, alpha]
        vector<float> spline;          //pointer to smoothed intentsity array
        vector<float> baseline;        //pointer to baseline array

        float maxIntensity;     //maxItensity in eics
        float totalIntensity;   //sum of all intensities in EIC
        int   eic_noNoiseObs;   //number of observatiosn above baseline.

        float mzmin;
        float mzmax;
        float rtmin;
        float rtmax;

        float baselineQCutVal; // computed by EIC::computeBaseline(). Deals with raw intensities (not with spline).

		Peak* addPeak(int peakPos);
		void deletePeak(unsigned int i);
        void getPeakPositions(int smoothWindow);
        void getPeakPositionsB(int smoothWindow, float minSmoothedPeakIntensity);
        void getPeakPositionsC(int smoothWindow, bool debug);
		void getPeakDetails(Peak& peak);
		void getPeakWidth(Peak& peak);
        void computeBaseLine(int smoothingWindow, int dropTopX);
        void computeSpline(int smoothWindow);
		void findPeakBounds(Peak& peak);
		void getPeakStatistics();
		void checkGaussianFit(Peak& peak);
        vector<Scan*> getFragmentationEvents();
		void subtractBaseLine();
		void removeOverlapingPeaks();
		EIC* clone(); //make a copy of self


		vector<mzPoint> getIntensityVector(Peak& peak);
		void summary();
        void setSmootherType(EIC::SmootherType x) { smootherType=x; }
        void setBaselineSmoothingWindow(int x) { baselineSmoothingWindow=x;}
        void setBaselineDropTopX(int x) { baselineDropTopX=x; }
        void interpolate();

		inline unsigned int size() { return intensity.size();}
		inline mzSample* getSample() { return sample; } 
        static vector<PeakGroup> groupPeaks(vector<EIC*>&eics, int smoothingWindow, float maxRtDiff);
        static vector<PeakGroup> groupPeaksB(vector<EIC*>&eics, int smoothingWindow, float maxRtDiff, float minSmoothedPeakIntensity);

		static EIC* eicMerge(const vector<EIC*>& eics);
		static void removeLowRankGroups(vector<PeakGroup>&groups, unsigned int rankLimit );
		static bool compMaxIntensity(EIC* a, EIC* b ) { return a->maxIntensity > b->maxIntensity; }

        private:
                SmootherType smootherType;
                int baselineSmoothingWindow;
                int baselineDropTopX;

};

class Peak {
	public:
		Peak();
		Peak(EIC* e, int p);
        Peak(const Peak& p);
		Peak& operator=(const Peak& o);
                void copyObj(const Peak& o);

		//~Peak() { cerr << "~Peak() " << this << endl; }

		unsigned int pos;
		unsigned int minpos;
		unsigned int maxpos;

		float rt;
		float rtmin;
		float rtmax;
		float mzmin;
		float mzmax;

		unsigned int scan;
		unsigned int minscan;
		unsigned int maxscan;

		float peakArea;                    //non corrected sum of all intensities
		float peakAreaCorrected;           //baseline substracted area
		float peakAreaTop;                  //top 3points of the peak
		float peakAreaFractional;          //area of the peak dived by total area in the EIC
		float peakRank;                     //peak rank (sorted by peakAreaCorrected)

		float peakIntensity;               //not corrected intensity at top of the pek
		float peakBaseLineLevel;            //baseline level below the highest point

		float peakMz;	//mz value at the top of the peak
		float medianMz;	//averaged mz value across all points in the peak
		float baseMz;	//mz value across base of the peak

		float quality;	//from 0 to 1. indicator of peak goodness
		unsigned int width;		//width of the peak at the baseline
		float gaussFitSigma;	//fit to gaussian curve
		float gaussFitR2;		//fit to gaussian curve
		int groupNum;

		unsigned int noNoiseObs; 
		float noNoiseFraction;
		float symmetry;
		float signalBaselineRatio;
		float groupOverlap;			// 0 no overlap, 1 perfect overlap
		float groupOverlapFrac;		

		int   	chargeState;		    	
		int   	ms2EventCount;
		float   selectionScore;
		bool    isMonoIsotopic;

		bool localMaxFlag;
		bool fromBlankSample;		//true if peak is from blank sample

		char label;		//classification label
        mzSample *sample;  //pointer to sample

    private:
		EIC* eic; 		//pointer to eic 

	public: 
		void setEIC(EIC* e) { eic=e; }
		inline EIC*	 getEIC() { return eic;    }
		inline bool hasEIC() { return eic != NULL; }
		Scan* getScan() { if(sample) return sample->getScan(scan); else return NULL; }	
        vector<Scan*> getFragmentationEvents(float ppmWindow);
        Fragment getConsensusFragmentation(float ppmWindow, float productPpmTolr);

		int getChargeState();

		void   setSample(mzSample* s ) { sample=s; }
		inline mzSample* getSample() { return sample; }
		inline bool hasSample() 	{  return sample != NULL; }
		
		void setLabel(char label) { this->label=label;}
		inline char getLabel() { return label;}
		
        static bool compRtMin(const Peak& a, const Peak& b ) { return a.rtmin < b.rtmin; }
		static bool compRt(const Peak& a, const Peak& b ) { return a.rt < b.rt; }
		static bool compIntensity(const Peak& a, const Peak& b ) { return b.peakIntensity < a.peakIntensity; }
		static bool compArea(const Peak& a, const Peak& b ) { return b.peakAreaFractional < a.peakAreaFractional; }
		static bool compMz(const Peak& a, const Peak& b ) { return a.peakMz < b.peakMz; }
		static bool compSampleName(const Peak& a, const Peak& b ) { return a.sample->getSampleName() < b.sample->getSampleName(); }
		static bool compSampleOrder(const Peak& a, const Peak& b ) { return a.sample->getSampleOrder() < b.sample->getSampleOrder(); }
		inline static float overlap(const Peak& a, const Peak& b) {	return( checkOverlap(a.rtmin, a.rtmax, b.rtmin, b.rtmax)); }
		vector<mzLink> findCovariants();
};

class PeakGroup {

	public:
        enum GroupType {None=0, C13Type=1, AdductType=2, CovariantType=3, IsotopeType=4 };     //group types
        enum QType	   {AreaTop=0, Area=1, Height=2, AreaNotCorrected=3, RetentionTime=4, Quality=5, SNRatio=6, MS2Count=7 };
		PeakGroup();
		PeakGroup(const PeakGroup& o);
		PeakGroup& operator=(const PeakGroup& o);
		bool operator==(const PeakGroup* o);
        bool operator==(const PeakGroup& o);
        void copyObj(const PeakGroup& o);

		~PeakGroup();

		PeakGroup* parent;
        Compound* compound;
        Adduct*   adduct;

		vector<Peak> peaks;
        vector<PeakGroup> children;

        string srmId;
        string tagString;
        string searchTableName;
        char label;			//classification label
        string displayName; //Issue 75: For use with tabledockwidget, other GUI displays
        string getName();               //compound name + tagString + srmid

        vector<char> labels; //Issue 127: for use with a more intricate tagging system

        bool isFocused;

        int groupId;
        int metaGroupId;

        float maxIntensity;
        float meanRt;
        float meanMz;
        int  ms2EventCount;

        //isotopic information
        float expectedAbundance;
        int   isotopeC13count;
        
        //Flag for deletion
        bool deletedFlag;

        float minRt;
        float maxRt;
        float minMz;
        float maxMz;

        float blankMax;
        float blankMean;
        unsigned int blankSampleCount;

        int sampleCount;
        float sampleMean;
        float sampleMax;

        unsigned int maxNoNoiseObs;
        unsigned int  maxPeakOverlap;
        float maxQuality;
        float maxPeakFracionalArea;
        float maxSignalBaseRatio;
        float maxSignalBaselineRatio;
        int goodPeakCount;
        float expectedRtDiff;
        float groupRank;

        //for sample contrasts  ratio and pvalue
        float changeFoldRatio;
        float changePValue;


        //LOSS
        int     chargeState;
        int     isotopicIndex;

        bool  	hasSrmId()  { return srmId.empty(); }
        void  	setSrmId(string id)	  { srmId=id; }
        inline  string getSrmId() { return srmId; }

        bool isPrimaryGroup();
        inline bool hasCompoundLink()  { if(compound != NULL) return true ; return false; }
        inline bool isEmpty()   { if(peaks.size() == 0) return true; return false; }
		inline unsigned int peakCount()  { return peaks.size(); 	  }
		inline unsigned int childCount() { return children.size(); }
		inline Compound* getCompound() { return compound; }

		inline PeakGroup* getParent() { return parent; }

		inline vector<Peak>& getPeaks() { return peaks; } 
        inline vector<PeakGroup>& getChildren()  { return children; }

        vector<Scan*> getRepresentativeFullScans();
        vector<Scan*> getFragmentationEvents();
        void computeFragPattern(float productPpmTolr);
        void findHighestPurityMS2Pattern(float precPpmTolr);
        Scan* getAverageFragmentationScan(float productPpmTolr);

        Peak* getHighestIntensityPeak();
        int getChargeStateFromMS1(float ppm);
        bool isMonoisotopic(float ppm);


		inline void setParent(PeakGroup* p) {parent=p;}
		inline void setLabel(char label) { this->label=label;}
        inline float ppmDist(float cmass) { return mzUtils::ppmDist(cmass,meanMz); }

		inline void addPeak(const Peak& peak) { peaks.push_back(peak); peaks.back().groupNum=groupId; }
		inline void addChild(const PeakGroup& child) { children.push_back(child); children.back().parent = this;   }
		Peak* getPeak(mzSample* sample);

		GroupType _type;
		inline GroupType type() { return _type; }
		inline void setType(GroupType t)  { _type = t; }
        inline bool isIsotope() { return _type == IsotopeType; }
        inline bool isAdduct() {  return _type == AdductType; }


		void summary();
		void groupStatistics();
		void updateQuality();
		float medianRt();
		float meanRtW();

        bool isGroupGood();
        bool isGroupBad();
        void markGroupGood();
        void markGroupBad();
        void addLabel(char label);
        void toggleLabel(char label);

		void reduce();
		void fillInPeaks(const vector<EIC*>& eics);
		void computeAvgBlankArea(const vector<EIC*>& eics);
		void groupOverlapMatrix();

        Peak* getSamplePeak(mzSample* sample);
        FragmentationMatchScore fragMatchScore;
        Fragment fragmentationPattern;

		void deletePeaks();
        bool deletePeak(unsigned int index);

		void clear();
		void deleteChildren();
        bool deleteChild(unsigned int index);
        bool deleteChild(PeakGroup* child);
        void copyChildren(const PeakGroup& other);

		vector<float> getOrderedIntensityVector(vector<mzSample*>& samples, QType type);
		void reorderSamples();

		static bool compRt(const PeakGroup& a, const PeakGroup& b ) { return(a.meanRt < b.meanRt); }
		static bool compMz(const PeakGroup& a, const PeakGroup& b ) { return(a.meanMz > b.meanMz); }
		static bool compIntensity(const PeakGroup& a, const PeakGroup& b ) { return(a.maxIntensity > b.maxIntensity); }
		static bool compArea(const PeakGroup& a, const PeakGroup& b ) { return(a.maxPeakFracionalArea > b.maxPeakFracionalArea); }
		static bool compQuality(const PeakGroup& a, const PeakGroup& b ) { return(a.maxQuality > b.maxQuality); }
		static bool compRankPtr(const PeakGroup* a, const PeakGroup* b ) { return(a->groupRank < b->groupRank); }
		static bool compRatio(const PeakGroup& a, const PeakGroup& b ) { return(a.changeFoldRatio < b.changeFoldRatio); }
		static bool compPvalue(const PeakGroup* a, const PeakGroup* b ) { return(a->changePValue< b->changePValue); }
		static bool compC13(const PeakGroup* a, const PeakGroup* b) { return(a->isotopeC13count < b->isotopeC13count); }
		static bool compMetaGroup(const PeakGroup& a, const PeakGroup& b) { return(a.metaGroupId < b.metaGroupId); }
		//static bool operator< (const PeakGroup* b) { return this->maxIntensity < b->maxIntensity; }
        static bool compMaxIntensity(const PeakGroup* a, const PeakGroup* b) { return(a->maxIntensity > b->maxIntensity); }
        static void clusterGroups(vector<PeakGroup> &allgroups, vector<mzSample*>samples, double maxRtDiff, double minSampleCorrelation, double minPeakShapeCorrelation, double ppm, vector<double> mzDeltas=vector<double>());
};

class Compound { 
        static MassCalculator* mcalc;

		private:
			PeakGroup _group;			//link to peak group
            bool      _groupUnlinked;
            float exactMass;

		public:
			Compound(string id, string name, string formula, int charge );
            ~Compound(){}; //empty destructor

            PeakGroup* getPeakGroup() { return &_group; }
			void setPeakGroup(const PeakGroup& group ) { _group = group; _group.compound = this; }
			bool hasGroup()    { if(_group.meanMz != 0 ) return true; return false; } 
			void clearGroup()  { _group.clear(); }
			void unlinkGroup() { _group.clear(); _groupUnlinked = true; }
            bool groupUnlinked() { return _groupUnlinked; }
            FragmentationMatchScore scoreCompoundHit(Fragment* f, float productPpmTolr, bool searchProton);

            int    cid;
            string id;
            string name;
            string formula;
            string smileString;
            string adductString;
            int    ionizationMode;

            string srmId;
            float expectedRt;
            int charge;

            //QQQ mapping
            string method_id;
            float precursorMz;	//QQQ parent ion
            float productMz;	//QQQ child ion
            float collisionEnergy; //QQQ collision energy
            float logP;

            string db;			//name of database for example KEGG, ECOCYC.. etc..
            bool  isDecoy;
            bool  virtualFragmentation;

            vector<float>fragment_mzs;
            vector<float>fragment_intensity;
            map<int,string>fragment_iontype;
            vector<string> category;

            float ajustedMass(int charge);
            static bool compMass(const Compound* a, const Compound* b )      { return(a->exactMass < b->exactMass);       }
            static bool compName(const Compound* a, const Compound* b )    { return(a->name < b->name);       }
            static bool compFormula(const Compound* a, const Compound* b ) { return(a->formula < b->formula); }

            inline float getExactMass() { return exactMass; }
            void setExactMass(float value) { exactMass = value; }

            inline void setFormala(string formulastr) { formula = formulastr; }
            string getFormula() { return formula; }
            map<string, string> metaDataMap = {};

            virtual vector<Compound*> getChildren();
};

class SummarizedCompound : public Compound {

public:
    vector<Compound*> children;
    string summarizedName;

    SummarizedCompound(string summarizedName, vector<Compound*> childrenCompounds) : Compound("", summarizedName, "", 0){
        children = childrenCompounds;
    }
    vector<Compound*> getChildren();

    //Relies on children
    void computeFragments();
    virtual ~SummarizedCompound(){}
};

class Isotope {
public:
    string name;
    double mz;
    double abundance;
    int charge;
    int N15;
    int C13;
    int S34;
    int H2;

    Isotope(string name, float mass, int c=0, int n=0, int s=0, int h=0) {
        this->mz=mass; this->name=name; charge=0;
        C13=c; N15=n; S34=s; H2=h;
    }

    Isotope() {
        mz=0; abundance=0; N15=0; C13=0; S34=0; H2=0; charge=0;
    }

    Isotope(const Isotope& b) {
        name=b.name;
        mz=b.mz;
        abundance=b.abundance;
        N15=b.N15; S34=b.S34; C13=b.C13; H2=b.H2;
        charge = b.charge;
    }

    Isotope& operator=(const Isotope& b) {
        name=b.name; mz=b.mz; abundance=b.abundance;
        N15=b.N15; S34=b.S34; C13=b.C13; H2=b.H2;
        charge =b.charge;
        return *this;
    }

};

class Adduct { 

	public:
        Adduct(){ mass=0; charge=1; nmol=1; }

        Adduct(string name, float mass, int charge, int nmol){
            this->name=name; this->mass=mass; this->charge=charge; this->nmol=nmol;
        }

        string name;
		int	  nmol;
		float mass;
		float charge;
		
        //given adduct mass compute parent ion mass
		inline float computeParentMass(float mz)  { return  (mz*abs(charge)-mass)/nmol; }
        //given perent compute adduct mass
		inline float computeAdductMass(float pmz) { return (pmz*nmol+mass)/abs(charge); }
};

struct AligmentSegment {
		string sampleName;
		float  seg_start;
		float  seg_end;
		float  new_start;
		float  new_end;
		float updateRt(float oldRt);

};
		

class Aligner {

	public:
		Aligner();
		void doAlignment(vector<PeakGroup*>& peakgroups);
		vector<double> groupMeanRt();
		double checkFit();
        void Fit(int ideg);
		void PolyFit(int maxdegree);
		void saveFit();
		void restoreFit();
		void setMaxItterations(int x) { maxItterations=x; }
		void setPolymialDegree(int x) { polynomialDegree=x; }
		void setSamples(vector<mzSample*>set) { samples=set; }

		void loadAlignmentFile(string alignmentFile); //load alignment information from a file
		void doSegmentedAligment();	 //ralign scans using guided alignment

		inline void addSegment(string sampleName, AligmentSegment* s) { 
			alignmentSegments[sampleName].push_back(s); 
		}

	private:
		vector< vector<float> > fit;
		vector<mzSample*> samples;
		vector<PeakGroup*> allgroups;
		map<string,vector<AligmentSegment*> > alignmentSegments;
		int maxItterations;
		int polynomialDegree;
		
};


class ChargedSpecies{
public:
    ChargedSpecies(){
        deconvolutedMass=0; minZ=0; maxZ=0; countMatches=0; error=0; upCount=0; downCount=0; scan=NULL; totalIntensity=0; isotopicClusterId=0;
        minRt=maxRt=meanRt=0; istotopicParentFlag=false; minIsotopeMass=0; maxIsotopeMass=0; massDiffectCluster=-100; filterOutFlag=false;
        qscore=0;
    }

    float deconvolutedMass;     //parent mass guess
    float error;
    int minZ;
    int maxZ;
    int countMatches;
    float totalIntensity;
    float qscore;
    int upCount;
    int downCount;
    Scan* scan;
    vector<float> observedMzs;
    vector<float> observedIntensities;
    vector<float> observedCharges;

    int isotopicClusterId;
    int massDiffectCluster;
    bool istotopicParentFlag;
    bool filterOutFlag;

    float meanRt;
    float minRt;
    float maxRt;

    vector<ChargedSpecies*> isotopes;
    int minIsotopeMass;
    int maxIsotopeMass;

    static bool compIntensity(ChargedSpecies* a, ChargedSpecies* b ) { return a->totalIntensity > b->totalIntensity; }
    static bool compMass(ChargedSpecies* a, ChargedSpecies* b ) { return a->deconvolutedMass < b->deconvolutedMass; }
    static bool compMatches(ChargedSpecies* a, ChargedSpecies* b ) { return a->countMatches > b->countMatches; }
    static bool compRt(ChargedSpecies* a, ChargedSpecies* b ) { return a->meanRt < b->meanRt; }

    static bool compMetaGroup(ChargedSpecies* a, ChargedSpecies* b ) {
        return (a->isotopicClusterId * 100000 + a->deconvolutedMass  < b->isotopicClusterId* 100000 + b->deconvolutedMass);
    }

    void updateIsotopeStatistics() {
        minIsotopeMass = maxIsotopeMass =deconvolutedMass;
        for(unsigned int i=0; i < isotopes.size(); i++) {
            if (isotopes[i]->deconvolutedMass < minIsotopeMass) minIsotopeMass=isotopes[i]->deconvolutedMass;
            if (isotopes[i]->deconvolutedMass > maxIsotopeMass) maxIsotopeMass=isotopes[i]->deconvolutedMass;
        }
    }
};

#endif
