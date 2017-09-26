#include "qrs_detection.h"

/*TODO: 
    move stuff in initialize that prepares/encrypts data
    create methods to match .h file
    finish making methods in .h file (calls to actual algorithms)
    write calls to algorithsm to call functions defined in h files (constructor() -> loop through specific (int iterations = samples.size/(nslots-n_considered)
    rethink through parameters/outputs in .h file new methods
    implement baby example in plain
    think through security of methods in .h -> reveal info by return values? can hide. 
    implement min_max based on that one paper's method 3
    change parameters (params.L calculation) based on expected levels needed for subtration/comparison. 
    double check that comparison works on neg numbers 
    if not, scale everything...? need to make sure that works... 
    

  */
QRS_Detection::QRS_Detection(vector<long> ecgs, int sampling_frequency, bool dbg){
	samples = ecgs;
    fs = sampling_frequency;
    debug = dbg;

    bits = 64;

	set_params();
	initialize();
}

const string QRS_Detection::className(){
	return "QRS_Detection";
}

void QRS_Detection::t_start(){
	if (debug){
		t.start();
	}
}

void QRS_Detection::t_end(string name){
	if (debug){
		double duration = t.end("silent");
		cout << className() << ": " << name << " - " << nslots << " operations done in " << duration << "s" << endl;
		cout << className() << ": " << name << " - Time for each operation: " << 1000*duration/nslots << "ms" << endl;
	}
}

Errors QRS_Detection::test_all(){
	Errors e("QRS_Detection");
	e.add("ds_fhe()", test_ds_fhe());
	e.add("ds_fhe(5)", test_ds_fhe(5));
	e.add("ds_fhe_unpacked()", test_ds_unpacked_fhe());
	e.add("ds_fhe_unpacked(5)", test_ds_unpacked_fhe(5));
	e.add("ds_plain()", test_ds_plain());
	e.add("ds_plain(5)", test_ds_plain(5));
	return e;
}

void QRS_Detection::set_params(){
	params.p = 2;
	params.r = 1;
	params.d = 0; // field p^d
	params.k = 128;
	params.slb = 800;
	params.L = 0;
	if(params.L == 0){ //L not set 
	//for the ripple carry adder so for circuits with complexity up to 3n+1
	//not valid for ripple comparator !
		switch(bits){
			case 1:
				params.L = 5;
				break;
			case 2:
				params.L = 7;
				break;
			case 3:
				params.L = 9;
				break;
			case 4:
				params.L = 12;
				break;
			case 5:
				params.L = 13;
				break;
			case 6:
				params.L = 15;
				break;
			case 7:
				params.L = 17;
				break;
			case 8:
				params.L = 19;
				break;
			case 9:
				params.L = 21;
				break;
			case 10:
				params.L = 23;
				break;
			case 11:
				params.L = 27;
				break;
			case 12:
				params.L = 29;
				break;
			case 13:
				params.L = 31;
				break;
			case 14:
				params.L = 34;
				break;
			case 15:
				params.L = 35;
				break;
			case 16:
				params.L = 37;
				break;
			if(params.L == 0){ // bits not in 1 .. 16
				params.L = 44; //should work with everything
			}
		}
	}
	
	if ((params.L > 42)&&(params.slb > 600)){
		params.L *= 1.2;
	}
	params.c = 3;
	params.w = 64;
	
	params.m = FindM(params.k,params.L,params.c,params.p,params.d,params.slb,0);
}
void QRS_Detection::initialize(){
    if (debug){
        cout << className() << ": Initializing..." << endl;
    }   

    double double_a = 0.027 * fs;   // set as in Wang's paper, an estimate of the min width of QRS complex
    a = round(double_a);
    double double_b = 0.063 * fs;   // set as in Wang's paper, an estimate of the max width of QRS complex
    b = round(double_b);
    
    n_considered = (2 * b) + 1;     // number of samples considered in one iteration (goes from 0 to -2b)
    lr_size = (b - a) + 1;          // convenience for looping through each side 

    diff_threshold = 3840 / fs;     // initial diff_threshold value. to be updated later based on S_ave
    min_threshold = 1536 / fs;      // threshold value given in paper
    avg_height = 0.0;               // set average height to 0. to be updated later based on sample heights

    // Constant values of 1/a, 1/(a+1), ... , 1/b. These are the values of the "run" in the rise over run calculation of slope
    double width;
    double rounded_width;

    for (double i = a; i < (b + 1); i++){
        width = 1/i;
        rounded_width = round(100000 * width)/100000;

        sample_difference_widths.push_back(rounded_width);
    }

    if (debug){
        cout << "a,b = " << a << "," << b << endl;
        cout << "n_considered = " << n_considered << endl;
        cout << "lr_size = " << lr_size << endl;

        for (int i = 0; i < lr_size; i++){
            cout << "sample_difference_widths[" << i << "]: " << sample_difference_widths[i] << endl;
        }
    }
	n_samples = samples.size(); //Total number of samples given to process 
	he.debug_on(debug);
	cout << className() << ": Number of bits n was set to " << bits << endl; 
	nslots = he.keyGen(params);
	mkt k_ones = he.setOnes(nslots);
	he.set01(k_ones);

	// Compute scaling_factors based on a and b values. 
    long x = 5354228880;
    // x = 5,354,228,880 = 2^4*3^2*5*7*11*13*17*19*23 = Minimum number divisible by 10, 11, ... , 23
    // Note: If fs != 360, then likely a != 10 and b != 23, which means x should be recomputed to be the minimum value such that x / a, x / (a+1), ... , x / b  all yield whole numbers 

    scaling_factors.push_back(x);
    for (int i = a; i <= b; i++){
        scaling_factors.push_back(x/i);
    }

    // Compute samples*x, samples*x/a, ... , samples*x/b
    for (int i = 0; i < nslots; i++){
        samples_x.push_back(samples[i]*scaling_factors[0]);
        samples_x_10.push_back(samples[i]*scaling_factors[1]);
        samples_x_11.push_back(samples[i]*scaling_factors[2]);
        samples_x_12.push_back(samples[i]*scaling_factors[3]);
        samples_x_13.push_back(samples[i]*scaling_factors[4]);
        samples_x_14.push_back(samples[i]*scaling_factors[5]);
        samples_x_15.push_back(samples[i]*scaling_factors[6]);
        samples_x_16.push_back(samples[i]*scaling_factors[7]);
        samples_x_17.push_back(samples[i]*scaling_factors[8]);
        samples_x_18.push_back(samples[i]*scaling_factors[9]);
        samples_x_19.push_back(samples[i]*scaling_factors[10]);
        samples_x_20.push_back(samples[i]*scaling_factors[11]);
        samples_x_21.push_back(samples[i]*scaling_factors[12]);
        samples_x_22.push_back(samples[i]*scaling_factors[13]);
        samples_x_23.push_back(samples[i]*scaling_factors[14]);
    }
    

    samples_bits_x.resize(bits, vector<long> (nslots,0));
    samples_bits_x_10.resize(bits, vector<long> (nslots,0));
    samples_bits_x_11.resize(bits, vector<long> (nslots,0));
    samples_bits_x_12.resize(bits, vector<long> (nslots,0));
    samples_bits_x_13.resize(bits, vector<long> (nslots,0));
    samples_bits_x_14.resize(bits, vector<long> (nslots,0));
    samples_bits_x_15.resize(bits, vector<long> (nslots,0));
    samples_bits_x_16.resize(bits, vector<long> (nslots,0));
    samples_bits_x_17.resize(bits, vector<long> (nslots,0));
    samples_bits_x_18.resize(bits, vector<long> (nslots,0));
    samples_bits_x_19.resize(bits, vector<long> (nslots,0));
    samples_bits_x_20.resize(bits, vector<long> (nslots,0));
    samples_bits_x_21.resize(bits, vector<long> (nslots,0));
    samples_bits_x_22.resize(bits, vector<long> (nslots,0));
    samples_bits_x_23.resize(bits, vector<long> (nslots,0));

    for (int i = 0; i < nslots; i++){
        bitset<64> b_x(samples_x[i]);
        bitset<64> b_x_10(samples_x_10[i]);
        bitset<64> b_x_11(samples_x_11[i]);
        bitset<64> b_x_12(samples_x_12[i]);
        bitset<64> b_x_13(samples_x_13[i]);
        bitset<64> b_x_14(samples_x_14[i]);
        bitset<64> b_x_15(samples_x_15[i]);
        bitset<64> b_x_16(samples_x_16[i]);
        bitset<64> b_x_17(samples_x_17[i]);
        bitset<64> b_x_18(samples_x_18[i]);
        bitset<64> b_x_19(samples_x_19[i]);
        bitset<64> b_x_20(samples_x_20[i]);
        bitset<64> b_x_21(samples_x_21[i]);
        bitset<64> b_x_22(samples_x_22[i]);
        bitset<64> b_x_23(samples_x_23[i]);
        for (int b = 0; b < bits; b++){
            samples_bits_x[b][i] = b_x[b];
            samples_bits_x_10[b][i] = b_x_10[b];
            samples_bits_x_11[b][i] = b_x_11[b];
            samples_bits_x_12[b][i] = b_x_12[b];
            samples_bits_x_13[b][i] = b_x_13[b];
            samples_bits_x_14[b][i] = b_x_14[b];
            samples_bits_x_15[b][i] = b_x_15[b];
            samples_bits_x_16[b][i] = b_x_16[b];
            samples_bits_x_17[b][i] = b_x_17[b];
            samples_bits_x_18[b][i] = b_x_18[b];
            samples_bits_x_19[b][i] = b_x_19[b];
            samples_bits_x_20[b][i] = b_x_20[b];
            samples_bits_x_21[b][i] = b_x_21[b];
            samples_bits_x_22[b][i] = b_x_22[b];
            samples_bits_x_23[b][i] = b_x_23[b];
        }
    }

	k_constant_x.resize(bits);
/*
    for (int i = 0; i < bits; i++){
        k_constant_x[i] = he.encrypt(samples_bits_x[i]);
        cout << "encrypted as: " << k_constant_x[i] << endl;
    }
*/ 

    inputs.resize(n_considered, vector < long > (nslots,0));
	v_in.resize(n_considered,vector< vector<long> >(bits,vector<long>(nslots,0)));
	k_constant.resize(n_considered, vector < mkt>(bits));

	//inputs to N bit circuits
	for(unsigned j = 0; j < nslots; j++){  
		    inputs[0][j] = samples_x[j];
            inputs[1][j] = samples_x_10[j];
	}
	
	if(debug){
        for(unsigned j = 0; j < nslots; j++){ 
			    cout << "inputs[0][" << j << "]: " << inputs[0][j] << endl;
                cout << "inputs[1][" << j << "]: " << inputs[1][j] << endl;
        }
    }
/*
	//Converts inputs to bits into v_in for parallel ciphertexts
	for(unsigned n = 0; n < n_considered; n++){
		for(unsigned j = 0; j < nslots; j++){
			bitset<64> bin(inputs[n][j]); //max is 2^64 so max nbits = 64
			for(unsigned b = 0; b < bits; b++){
				v_in[n][b][j] = bin[b]; //first ctxt (b = 0) is LSB
			}
		}
	}
	
	//Encrypts all the vectors into ciphertexts
	if(debug){
		cout << className() << ": Encrypting input vectors (" << n_considered * bits << " vectors)" << endl;
	}
	for(unsigned n = 0; n < n_considered; n++){
		for (unsigned b = 0; b < bits; b++){
			k_constant[n][b] = he.encrypt(v_in[n][b]);
			if(debug){
				cout << "k_constant[" << n << "][" << b << "]: " << k_constant[n][b] << endl; 
			}
		}
	}*/
}

void QRS_Detection::make_copies(vector< vector<mkt> > input, vector< vector<mkt> > destination){
	for(unsigned n = 0; n < destination.size(); n++){
		for (unsigned b = 0; b < destination[n].size(); b++){
			he.erase(destination[n][b]);
		}
	}
	destination = vector< vector<mkt> >(input.size(), vector<mkt>(input[0].size())); // used to be (n_considered, vector<mkt>(bits))
	for(unsigned n = 0; n < input.size(); n++){
		for (unsigned b = 0; b < input[n].size(); b++){
			destination[n][b] = he.copy(input[n][b]);
		}
	}
}

void QRS_Detection::prepare_data(vector<long> sample_subset){

}

void QRS_Detection::compute_lr_slopes(vector< vector<mkt> > encrypted_pairs, bool simd){

}

void QRS_Detection::compute_lr_slopes(vector< vector<long> > plain_pairs){

}

vector<mkt> QRS_Detection::compute_min_max(vector<mkt> encrypted_list){
    vector<mkt> result;


    return result;
}

vector<long> QRS_Detection::compute_min_max(vector<long> plain_list){
    vector<long> result; 


    return result;
}

vector<mkt> QRS_Detection::compute_diff_max(vector<mkt> encrypted_mins_maxs){
    vector<mkt> result;

    
    return result;
}

vector<long> QRS_Detection::compute_diff_max(vector<long> plain_mins_maxs){
    vector<long> result;


    return result;
}

vector<mkt> QRS_Detection::compare_to_thresholds(vector<mkt> diff_maxs){
    vector<mkt> result;

    
    return result;
}

vector<bool> QRS_Detection::compare_to_thresholds(vector<long> diff_maxs){
    vector<bool> result;


    return result;
}

mkt QRS_Detection::check_peak_closeness(vector<mkt> peaks){
    mkt true_peak;


    return true_peak;
}

long QRS_Detection::check_peak_closeness(vector<long> peaks){
    long true_peak;


    return true_peak;
}

vector<mkt> QRS_Detection::update_thresholds(vector<mkt> diff_maxs){
    vector<mkt> results;


    return results;
}

void QRS_Detection::update_thresholds(long diff_max){

}

/**********************DUALSLOPE*************************/

void QRS_Detection::ds_fhe(){
    int iterations_needed = n_samples / (nslots - n_considered);
    iterations++; // 
    if (debug){
        cout << "iterations_needed = " << iterations_needed << endl;
    }
}

void QRS_Detection::ds_fhe(int iterations){

    cout << "in ds_fhe(iterations), iterations = " << iterations << endl;
}

void QRS_Detection::ds_unpacked_fhe(){

    cout << "in ds_unpacked_fhe()" << endl;
}

void QRS_Detection::ds_unpacked_fhe(int iterations){

    cout << "in ds_unpacked_fhe(iterations), iterations = " << iterations << endl;
}

void QRS_Detection::ds_plain(){

    cout << "in ds_plain()" << endl;
}

void QRS_Detection::ds_plain(int iterations){
    cout << "in ds_plain(iterations), iterations = " << iterations << endl;
}

/***********************TESTING**************************/

bool QRS_Detection::test_ds_fhe(){
    ds_fhe();
    return false;
}

bool QRS_Detection::test_ds_fhe(int iterations){
    ds_fhe(iterations);
    return false;
}

bool QRS_Detection::test_ds_unpacked_fhe(){
    ds_unpacked_fhe();
    return false;
}

bool QRS_Detection::test_ds_unpacked_fhe(int iterations){
    ds_unpacked_fhe(iterations);
    return false;
}

bool QRS_Detection::test_ds_plain(){
    ds_plain();
    return false;
}

bool QRS_Detection::test_ds_plain(int iterations){
    ds_plain(iterations);
    return false;
}
