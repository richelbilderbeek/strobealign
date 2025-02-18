#ifndef aln_hpp
#define aln_hpp

#include <string>
#include <vector>
#include "kseq++.hpp"
#include "index.hpp"
#include "refs.hpp"
#include "sam.hpp"

struct alignment_params {
    int match;
    int mismatch;
    int gap_open;
    int gap_extend;
};

struct aln_info {
    std::string cigar;
    unsigned int ed;
    unsigned int ref_offset;
    int sw_score;
    int global_ed;
    int length;
};

struct AlignmentStatistics {
    std::chrono::duration<double> tot_read_file;
    std::chrono::duration<double> tot_construct_strobemers;
    std::chrono::duration<double> tot_find_nams;
    std::chrono::duration<double> tot_time_rescue;
    std::chrono::duration<double> tot_find_nams_alt;
    std::chrono::duration<double> tot_sort_nams;
    std::chrono::duration<double> tot_extend;
    std::chrono::duration<double> tot_write_file;

    unsigned int n_reads = 0;
    unsigned int tot_ksw_aligned = 0;
    unsigned int tot_rescued = 0;
    unsigned int tot_all_tried = 0;
    unsigned int did_not_fit = 0;
    unsigned int tried_rescue = 0;

    AlignmentStatistics operator+=(const AlignmentStatistics& other) {
        this->tot_read_file += other.tot_read_file;
        this->tot_construct_strobemers += other.tot_construct_strobemers;
        this->tot_find_nams += other.tot_find_nams;
        this->tot_time_rescue += other.tot_time_rescue;
        this->tot_find_nams_alt += other.tot_find_nams_alt;
        this->tot_sort_nams += other.tot_sort_nams;
        this->tot_extend += other.tot_extend;
        this->tot_write_file += other.tot_write_file;
        this->n_reads += other.n_reads;
        this->tot_ksw_aligned += other.tot_ksw_aligned;
        this->tot_rescued += other.tot_rescued;
        this->tot_all_tried += other.tot_all_tried;
        this->did_not_fit += other.did_not_fit;
        this->tried_rescue += other.tried_rescue;
        return *this;
    }
};

struct mapping_params {
    int r { 150 };
    int max_secondary { 0 };
    float dropoff_threshold { 0.5 };
    int R { 2 };
    int maxTries { 20 };
    int rescue_cutoff;
    bool is_sam_out { true };
    bool output_unmapped { true };
};

class i_dist_est {
public:
    float sample_size = 1;
    float mu = 300;
    float sigma = 100;
    float V = 10000;
    float SSE = 10000;

    // Add a new observation
    void update(int dist);
};

void align_PE_read(const klibpp::KSeq& record1, const klibpp::KSeq& record2, Sam& sam, std::string& outstring, AlignmentStatistics& statistics, i_dist_est& isize_est, const alignment_params& aln_params, const mapping_params& map_param, const IndexParameters& index_parameters, const References& references, const StrobemerIndex& index);

void align_SE_read(const klibpp::KSeq& record, Sam& sam, std::string& outstring, AlignmentStatistics& statistics, const alignment_params& aln_params, const mapping_params& map_param, const IndexParameters& index_parameters, const References& references, const StrobemerIndex& index);

bool has_shared_substring(const std::string& read_seq, const std::string& ref_seq, int k);

#endif
