#include "nam.hpp"

struct hit {
    int query_s;
    int query_e;
    int ref_s;
    int ref_e;
    bool is_rc = false;
};

struct Hit {
    unsigned int count;
    RandstrobeMapEntry randstrobe_map_entry;
    unsigned int query_s;
    unsigned int query_e;
    bool is_rc;

    bool operator< (const Hit& rhs) const {
        return std::tie(count, query_s, query_e, is_rc)
            < std::tie(rhs.count, rhs.query_s, rhs.query_e, rhs.is_rc);
    }
};

void add_to_hits_per_ref(
    robin_hood::unordered_map<unsigned int, std::vector<hit>>& hits_per_ref,
    int query_s,
    int query_e,
    bool is_rc,
    const StrobemerIndex& index,
    RandstrobeMapEntry randstrobe_map_entry,
    int min_diff
) {
    // Determine whether the hash table’s value directly represents a
    // ReferenceMer (this is the case if count==1) or an offset/count
    // pair that refers to entries in the flat_vector.
    if (randstrobe_map_entry.is_direct()) {
        auto r = randstrobe_map_entry.as_ref_randstrobe();
        int ref_s = r.position;
        int ref_e = r.position + r.strobe2_offset() + index.k();
        int diff = std::abs((query_e - query_s) - (ref_e - ref_s));
        if (diff <= min_diff) {
            hits_per_ref[r.reference_index()].push_back(hit{query_s, query_e, ref_s, ref_e, is_rc});
            min_diff = diff;
        }
    } else {
        for (size_t j = randstrobe_map_entry.offset(); j < randstrobe_map_entry.offset() + randstrobe_map_entry.count(); ++j) {
            auto r = index.flat_vector[j];
            int ref_s = r.position;
            int ref_e = r.position + r.strobe2_offset() + index.k();

            int diff = std::abs((query_e - query_s) - (ref_e - ref_s));
            if (diff <= min_diff) {
                hits_per_ref[r.reference_index()].push_back(hit{query_s, query_e, ref_s, ref_e, is_rc});
                min_diff = diff;
            }
        }
    }
}

std::pair<float,int> find_nams(
    std::vector<nam> &final_nams,
    const QueryRandstrobeVector &query_randstrobes,
    const StrobemerIndex& index
) {
    robin_hood::unordered_map<unsigned int, std::vector<hit>> hits_per_ref;
    hits_per_ref.reserve(100);

    int nr_good_hits = 0, total_hits = 0;
    for (auto &q : query_randstrobes) {
        auto ref_hit = index.find(q.hash);
        if (ref_hit != index.end()) {
            total_hits++;
            if (ref_hit->second.count() > index.filter_cutoff) {
                continue;
            }
            nr_good_hits++;
            add_to_hits_per_ref(hits_per_ref, q.start, q.end, q.is_reverse, index, ref_hit->second, 100000);
        }
    }

    std::pair<float,int> info (0.0f,0); // (nr_nonrepetitive_hits/total_hits, max_nam_n_hits)
    info.first = total_hits > 0 ? ((float) nr_good_hits) / ((float) total_hits) : 1.0;
    int max_nam_n_hits = 0;
    int nam_id_cnt = 0;
    std::vector<nam> open_nams;

    for (auto &it : hits_per_ref) {
        auto ref_id = it.first;
        const std::vector<hit>& hits = it.second;
        open_nams = std::vector<nam> (); // Initialize vector
        unsigned int prev_q_start = 0;
        for (auto &h : hits){
            bool is_added = false;
            for (auto & o : open_nams) {

                // Extend NAM
                if (( o.is_rc == h.is_rc) && (o.query_prev_hit_startpos < h.query_s) && (h.query_s <= o.query_e ) && (o.ref_prev_hit_startpos < h.ref_s) && (h.ref_s <= o.ref_e) ){
                    if ( (h.query_e > o.query_e) && (h.ref_e > o.ref_e) ) {
                        o.query_e = h.query_e;
                        o.ref_e = h.ref_e;
//                        o.previous_query_start = h.query_s;
//                        o.previous_ref_start = h.ref_s; // keeping track so that we don't . Can be caused by interleaved repeats.
                        o.query_prev_hit_startpos = h.query_s; // log the last strobemer hit in case of outputting paf
                        o.ref_prev_hit_startpos = h.ref_s; // log the last strobemer hit in case of outputting paf
                        o.n_hits ++;
//                        o.score += (float)1/ (float)h.count;
                        is_added = true;
                        break;
                    }
                    else if ((h.query_e <= o.query_e) && (h.ref_e <= o.ref_e)) {
//                        o.previous_query_start = h.query_s;
//                        o.previous_ref_start = h.ref_s; // keeping track so that we don't . Can be caused by interleaved repeats.
                        o.query_prev_hit_startpos = h.query_s; // log the last strobemer hit in case of outputting paf
                        o.ref_prev_hit_startpos = h.ref_s; // log the last strobemer hit in case of outputting paf
                        o.n_hits ++;
//                        o.score += (float)1/ (float)h.count;
                        is_added = true;
                        break;
                    }
                }

            }
//            }
            // Add the hit to open matches
            if (!is_added){
                nam n;
                n.nam_id = nam_id_cnt;
                nam_id_cnt ++;
                n.query_s = h.query_s;
                n.query_e = h.query_e;
                n.ref_s = h.ref_s;
                n.ref_e = h.ref_e;
                n.ref_id = ref_id;
//                n.previous_query_start = h.query_s;
//                n.previous_ref_start = h.ref_s;
                n.query_prev_hit_startpos = h.query_s;
                n.ref_prev_hit_startpos = h.ref_s;
                n.n_hits = 1;
                n.is_rc = h.is_rc;
//                n.score += (float)1 / (float)h.count;
                open_nams.push_back(n);
            }

            // Only filter if we have advanced at least k nucleotides
            if (h.query_s > prev_q_start + index.k()) {

                // Output all NAMs from open_matches to final_nams that the current hit have passed
                for (auto &n : open_nams) {
                    if (n.query_e < h.query_s) {
                        int n_max_span = std::max(n.query_span(), n.ref_span());
                        int n_min_span = std::min(n.query_span(), n.ref_span());
                        float n_score;
                        n_score = ( 2*n_min_span -  n_max_span) > 0 ? (float) (n.n_hits * ( 2*n_min_span -  n_max_span) ) : 1;   // this is really just n_hits * ( min_span - (offset_in_span) ) );
//                        n_score = n.n_hits * n.query_span();
                        n.score = n_score;
                        final_nams.push_back(n);
                        max_nam_n_hits = std::max(n.n_hits, max_nam_n_hits);
                    }
                }

                // Remove all NAMs from open_matches that the current hit have passed
                auto c = h.query_s;
                auto predicate = [c](decltype(open_nams)::value_type const &nam) { return nam.query_e < c; };
                open_nams.erase(std::remove_if(open_nams.begin(), open_nams.end(), predicate), open_nams.end());
                prev_q_start = h.query_s;
            }
        }

        // Add all current open_matches to final NAMs
        for (auto &n : open_nams){
            int n_max_span = std::max(n.query_span(), n.ref_span());
            int n_min_span = std::min(n.query_span(), n.ref_span());
            float n_score;
            n_score = ( 2*n_min_span -  n_max_span) > 0 ? (float) (n.n_hits * ( 2*n_min_span -  n_max_span) ) : 1;   // this is really just n_hits * ( min_span - (offset_in_span) ) );
//            n_score = n.n_hits * n.query_span();
            n.score = n_score;
            final_nams.push_back(n);
            max_nam_n_hits = std::max(n.n_hits, max_nam_n_hits);
        }
    }
    info.second = max_nam_n_hits;
    return info;
}

void find_nams_rescue(
    std::vector<nam> &final_nams,
    const QueryRandstrobeVector &query_randstrobes,
    const StrobemerIndex& index,
    unsigned int filter_cutoff
) {
    robin_hood::unordered_map<unsigned int, std::vector<hit>> hits_per_ref;
    std::vector<Hit> hits_fw;
    std::vector<Hit> hits_rc;
    hits_per_ref.reserve(100);
    hits_fw.reserve(5000);
    hits_rc.reserve(5000);

    for (auto &q : query_randstrobes) {
        auto ref_hit = index.find(q.hash);
        if (ref_hit != index.end()) {
            Hit s{ref_hit->second.count(), ref_hit->second, q.start, q.end, q.is_reverse};
            if (q.is_reverse){
                hits_rc.push_back(s);
            } else {
                hits_fw.push_back(s);
            }
        }
    }

    std::sort(hits_fw.begin(), hits_fw.end());
    std::sort(hits_rc.begin(), hits_rc.end());
    for (auto& hits : {hits_fw, hits_rc}) {
        int cnt = 0;
        for (auto &q : hits) {
            auto count = q.count;
            if ((count > filter_cutoff && cnt >= 5) || count > 1000) {
                break;
            }
            add_to_hits_per_ref(hits_per_ref, q.query_s, q.query_e, q.is_rc, index, q.randstrobe_map_entry, 1000);
            cnt++;
        }
    }

    std::vector<nam> open_nams;
    int nam_id_cnt = 0;

    for (auto &it : hits_per_ref) {
        auto ref_id = it.first;
        std::vector<hit>& hits = it.second;
        std::sort(hits.begin(), hits.end(), [](const hit& a, const hit& b) -> bool {
                // first sort on query starts, then on reference starts
                return (a.query_s < b.query_s) || ( (a.query_s == b.query_s) && (a.ref_s < b.ref_s) );
            }
        );
        open_nams = std::vector<nam> (); // Initialize vector
        unsigned int prev_q_start = 0;
        for (auto &h : hits){
            bool is_added = false;
            for (auto & o : open_nams) {
                // Extend NAM
                if (o.is_rc == h.is_rc && o.query_prev_hit_startpos < h.query_s && h.query_s <= o.query_e && o.ref_prev_hit_startpos < h.ref_s && h.ref_s <= o.ref_e) {
                    if (h.query_e > o.query_e && h.ref_e > o.ref_e) {
                        o.query_e = h.query_e;
                        o.ref_e = h.ref_e;
//                        o.previous_query_start = h.query_s;
//                        o.previous_ref_start = h.ref_s; // keeping track so that we don't . Can be caused by interleaved repeats.
                        o.query_prev_hit_startpos = h.query_s; // log the last strobemer hit in case of outputting paf
                        o.ref_prev_hit_startpos = h.ref_s; // log the last strobemer hit in case of outputting paf
                        o.n_hits ++;
//                        o.score += (float)1/ (float)h.count;
                        is_added = true;
                        break;
                    }
                    else if (h.query_e <= o.query_e && h.ref_e <= o.ref_e) {
//                        o.previous_query_start = h.query_s;
//                        o.previous_ref_start = h.ref_s; // keeping track so that we don't . Can be caused by interleaved repeats.
                        o.query_prev_hit_startpos = h.query_s; // log the last strobemer hit in case of outputting paf
                        o.ref_prev_hit_startpos = h.ref_s; // log the last strobemer hit in case of outputting paf
                        o.n_hits ++;
//                        o.score += (float)1/ (float)h.count;
                        is_added = true;
                        break;
                    }
                }
            }
            // Add the hit to open matches
            if (!is_added){
                nam n;
                n.nam_id = nam_id_cnt;
                nam_id_cnt ++;
                n.query_s = h.query_s;
                n.query_e = h.query_e;
                n.ref_s = h.ref_s;
                n.ref_e = h.ref_e;
                n.ref_id = ref_id;
//                n.previous_query_start = h.query_s;
//                n.previous_ref_start = h.ref_s;
                n.query_prev_hit_startpos = h.query_s;
                n.ref_prev_hit_startpos = h.ref_s;
                n.n_hits = 1;
                n.is_rc = h.is_rc;
//                n.score += (float)1/ (float)h.count;
                open_nams.push_back(n);
            }

            // Only filter if we have advanced at least k nucleotides
            if (h.query_s > prev_q_start + index.k()) {

                // Output all NAMs from open_matches to final_nams that the current hit have passed
                for (auto &n : open_nams) {
                    if (n.query_e < h.query_s) {
                        int n_max_span = std::max(n.query_span(), n.ref_span());
                        int n_min_span = std::min(n.query_span(), n.ref_span());
                        float n_score;
                        n_score = ( 2*n_min_span -  n_max_span) > 0 ? (float) (n.n_hits * ( 2*n_min_span -  n_max_span) ) : 1;   // this is really just n_hits * ( min_span - (offset_in_span) ) );
//                        n_score = n.n_hits * n.query_span();
                        n.score = n_score;
                        final_nams.push_back(n);
                    }
                }

                // Remove all NAMs from open_matches that the current hit have passed
                auto c = h.query_s;
                auto predicate = [c](decltype(open_nams)::value_type const &nam) { return nam.query_e < c; };
                open_nams.erase(std::remove_if(open_nams.begin(), open_nams.end(), predicate), open_nams.end());
                prev_q_start = h.query_s;
            }
        }

        // Add all current open_matches to final NAMs
        for (auto &n : open_nams){
            int n_max_span = std::max(n.query_span(), n.ref_span());
            int n_min_span = std::min(n.query_span(), n.ref_span());
            float n_score;
            n_score = ( 2*n_min_span -  n_max_span) > 0 ? (float) (n.n_hits * ( 2*n_min_span -  n_max_span) ) : 1;   // this is really just n_hits * ( min_span - (offset_in_span) ) );
//            n_score = n.n_hits * (n.query_e - n.query_s);
            n.score = n_score;
            final_nams.push_back(n);
        }
    }
}
