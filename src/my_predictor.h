// my_predictor.h
// This file contains a sample gshare_predictor class.
// It is a simple 32,768-entry gshare with a history length of 15.

class gshare_update : public branch_update {
public:
	unsigned int index;
};

class gshare_predictor : public branch_predictor {
public:
#define HISTORY_LENGTH	15
#define TABLE_BITS	15
	gshare_update u;
	branch_info bi;
	unsigned int history;
	unsigned char tab[1<<TABLE_BITS];

	gshare_predictor (void) : history(0) { 
		memset (tab, 0, sizeof (tab));
	}

	branch_update *predict (branch_info & b) {
		bi = b;
		if (b.br_flags & BR_CONDITIONAL) {
			u.index = 
				  (history << (TABLE_BITS - HISTORY_LENGTH)) 
				^ (b.address & ((1<<TABLE_BITS)-1));
			u.direction_prediction (tab[u.index] >> 1);
		} else {
			u.direction_prediction (true);
		}
		u.target_prediction (0);
		return &u;
	}

	void update (branch_update *u, bool taken, unsigned int target) {
		if (bi.br_flags & BR_CONDITIONAL) {
			unsigned char *c = &tab[((gshare_update*)u)->index];
			if (taken) {
				if (*c < 3) (*c)++;
			} else {
				if (*c > 0) (*c)--;
			}
			history <<= 1;
			history |= taken;
			history &= (1<<HISTORY_LENGTH)-1;
		}
	}
};

//
// Pentium M hybrid branch predictors
// This class implements a simple hybrid branch predictor based on the Pentium M branch outcome prediction units. 
// Instead of implementing the complete Pentium M branch outcome predictors, the class below implements a hybrid 
// predictor that combines a bimodal predictor and a global predictor. 
class pm_update : public branch_update {
public:
        unsigned int bim_index;   // index into bimodal table
        unsigned int g_row;       // row (set) in global predictor
        unsigned int g_tag;       // tag for global predictor
        int          g_way;       // way that hit (-1 = miss)
        bool         bim_pred;    // bimodal prediction
        bool         g_pred;      // global prediction (valid only on hit)
};

// Hybrid predictor = bimodal table + 2-way set-associative tagged global
// predictor (simplified Pentium M).  If the global predictor hits, its
// prediction is used; otherwise the bimodal prediction is used.
class pm_predictor : public branch_predictor {
public:
// All sizes can be overridden at compile time, e.g.  make CXXFLAGS="-O3 -DPM_WAYS=4"
#ifndef PM_BIM_BITS
#define PM_BIM_BITS     12              // 4096-entry bimodal table
#endif
#ifndef PM_HIST_LEN
#define PM_HIST_LEN     15              // global history register length
#endif
#ifndef PM_ROW_BITS
#define PM_ROW_BITS     9               // 512 sets in global predictor
#endif
#ifndef PM_TAG_BITS
#define PM_TAG_BITS     6               // 6-bit tags (as in Pentium M)
#endif
#ifndef PM_WAYS
#define PM_WAYS         2               // 2-way (allowed simplification); 4 = Pentium M
#endif

        struct g_entry {
                bool          valid;
                unsigned int  tag;
                unsigned char ctr;      // 2-bit saturating counter
                unsigned char lru;      // age: 0 = most recently used
        };

        pm_update     u;
        branch_info   bi;
        unsigned int  ghr;
        unsigned char bim[1 << PM_BIM_BITS];
        g_entry       gtab[1 << PM_ROW_BITS][PM_WAYS];

        pm_predictor (void) : ghr(0) {
                // bimodal counters start at weakly taken (2)
                memset (bim, 2, sizeof (bim));
                for (int r = 0; r < (1 << PM_ROW_BITS); r++)
                        for (int w = 0; w < PM_WAYS; w++) {
                                gtab[r][w].valid = false;
                                gtab[r][w].tag   = 0;
                                gtab[r][w].ctr   = 0;
                                gtab[r][w].lru   = w;
                        }
        }

        static void sat_update (unsigned char &c, bool taken) {
                if (taken) { if (c < 3) c++; }
                else       { if (c > 0) c--; }
        }

        void touch (unsigned int row, int way) {
                // true LRU: entries younger than the used one age by 1,
                // the used way becomes MRU (age 0)
                unsigned char old = gtab[row][way].lru;
                for (int w = 0; w < PM_WAYS; w++)
                        if (gtab[row][w].lru < old) gtab[row][w].lru++;
                gtab[row][way].lru = 0;
        }

        branch_update *predict (branch_info & b) {
                bi = b;
                if (b.br_flags & BR_CONDITIONAL) {
                        // word address (drop the 2 low bits, like the example)
                        unsigned int pc = b.address >> 2;

                        // ---- bimodal ----
                        u.bim_index = pc & ((1 << PM_BIM_BITS) - 1);
                        u.bim_pred  = bim[u.bim_index] >> 1;

                        // ---- global: index/tag from PC xor GHR ----
                        unsigned int h = pc ^ ghr;
                        u.g_tag = h & ((1 << PM_TAG_BITS) - 1);
                        u.g_row = (h >> PM_TAG_BITS) & ((1 << PM_ROW_BITS) - 1);
                        u.g_way = -1;
#ifndef PM_NO_GLOBAL
                        for (int w = 0; w < PM_WAYS; w++) {
                                g_entry &e = gtab[u.g_row][w];
                                if (e.valid && e.tag == u.g_tag) {
                                        u.g_way  = w;
                                        u.g_pred = e.ctr >> 1;
                                        break;
                                }
                        }
#endif

                        // ---- final choice: global on hit, else bimodal ----
                        u.direction_prediction (u.g_way >= 0 ? u.g_pred : u.bim_pred);
                } else {
                        u.direction_prediction (true);
                }
                u.target_prediction (0);        // target prediction skipped
                return &u;
        }

        void update (branch_update *bu, bool taken, unsigned int target) {
                if (!(bi.br_flags & BR_CONDITIONAL)) return;
                pm_update *p = (pm_update *) bu;

                // bimodal always trained
                sat_update (bim[p->bim_index], taken);

                if (p->g_way >= 0) {
                        // hit: train the counter, update LRU
                        sat_update (gtab[p->g_row][p->g_way].ctr, taken);
                        touch (p->g_row, p->g_way);
                }
#ifndef PM_NO_GLOBAL
                else if (p->bim_pred != taken) {
                        // miss and bimodal was wrong: allocate in LRU way
                        int victim = 0;
                        for (int w = 0; w < PM_WAYS; w++)
                                if (!gtab[p->g_row][w].valid) { victim = w; goto found; }
                        for (int w = 0; w < PM_WAYS; w++)
                                if (gtab[p->g_row][w].lru > gtab[p->g_row][victim].lru) victim = w;
                found:
                        g_entry &e = gtab[p->g_row][victim];
                        e.valid = true;
                        e.tag   = p->g_tag;
                        e.ctr   = taken ? 2 : 1;        // weakly toward outcome
                        touch (p->g_row, victim);
                }
#endif

                // shift outcome into global history
                ghr = ((ghr << 1) | (taken ? 1 : 0)) & ((1 << PM_HIST_LEN) - 1);
        }
};

//
// Complete Pentium M branch predictors for extra credit
// This class implements the complete Pentium M branch prediction units.
// It implements both branch target prediction and branch outcome predicton.
//
// NOTE: this is a PARTIAL model of the Pentium M predictor (extended version
// of pm_predictor), not a cycle-exact reproduction of the ISPASS 2009 paper.
//
// Modelled:
//   Direction:  loop predictor  >  global predictor  >  bimodal predictor
//     * bimodal + 2-way global predictor: reused from pm_predictor above
//       (global index = address XOR global history, not the paper's PIR hash)
//     * loop predictor: 128 entries, direct-mapped, 14-bit tags, trip-count
//       learning with confidence, and a usefulness filter (own organisation;
//       the paper's loop predictor is organised differently)
//   Target:     BTB, 2048 entries, 4-way, true LRU, tagged; stores the taken
//               target and the fall-through address of each branch
// Not modelled: indirect-branch BTB (iBTB), the path information register
//               (PIR) and its hash with the IP, return stack buffer.
class cpm_update : public branch_update {
public:
        unsigned int index;
        bool         loop_hit;          // loop predictor entry found and confident
        bool         loop_pred;
        bool         base_pred;         // pm_predictor (global/bimodal) prediction
        unsigned int pred_target;       // copy of the predicted target (see measure_target.cc)
};

class cpm_predictor : public branch_predictor {
public:
#define LP_BITS         7               // 128-entry loop predictor
#define LP_TAG_BITS     14
#define LP_CONF_MAX     3
#define LP_CONF_USE     3               // need 3 identical trip counts in a row
#define BTB_SETS_BITS   9               // 512 sets x 4 ways = 2048-entry BTB
#define BTB_WAYS        4

        struct loop_entry {
                bool          valid;
                unsigned int  tag;
                unsigned int  trip;     // learned number of taken iterations
                unsigned int  iter;     // taken iterations seen in current instance
                unsigned char conf;     // confidence (0..3)
                signed char   useful;   // +: loop beat base predictor, -: it lost
        };
        struct btb_entry {
                bool          valid;
                unsigned int  tag;
                unsigned int  target;   // taken target
                unsigned int  fallthru; // not-taken (fall-through) address, 0 = unknown
                unsigned char lru;      // age, 0 = MRU
        };

        cpm_update   u;
        branch_info  bi;
        pm_predictor core;              // bimodal + global outcome predictor
        loop_entry   lp[1 << LP_BITS];
        btb_entry    btb[1 << BTB_SETS_BITS][BTB_WAYS];

        cpm_predictor (void) {
                memset (lp, 0, sizeof (lp));
                for (int s = 0; s < (1 << BTB_SETS_BITS); s++)
                        for (int w = 0; w < BTB_WAYS; w++) {
                                btb[s][w].valid = false;
                                btb[s][w].lru = w;
                        }
        }

        unsigned int lp_index (unsigned int a) { return (a >> 2) & ((1 << LP_BITS) - 1); }
        unsigned int lp_tag   (unsigned int a) { return (a >> (2 + LP_BITS)) & ((1 << LP_TAG_BITS) - 1); }
        unsigned int btb_set  (unsigned int a) { return (a >> 2) & ((1 << BTB_SETS_BITS) - 1); }
        unsigned int btb_tag  (unsigned int a) { return a >> (2 + BTB_SETS_BITS); }

        void btb_touch (unsigned int set, int way) {
                unsigned char old = btb[set][way].lru;
                for (int w = 0; w < BTB_WAYS; w++)
                        if (btb[set][w].lru < old) btb[set][w].lru++;
                btb[set][way].lru = 0;
        }

        branch_update *predict (branch_info & b) {
                bi = b;
                // ---------------- direction ----------------
                if (b.br_flags & BR_CONDITIONAL) {
                        u.base_pred = core.predict (b)->direction_prediction ();
                        loop_entry &e = lp[lp_index (b.address)];
                        u.loop_hit = e.valid && e.tag == lp_tag (b.address) && e.conf >= LP_CONF_USE;
                        if (u.loop_hit) u.loop_pred = (e.iter != e.trip);   // exit at trip count
                        // the loop predictor overrides only once it has proven
                        // better than the global/bimodal prediction for this branch
                        bool use_loop = u.loop_hit && e.useful > 0;
                        u.direction_prediction (use_loop ? u.loop_pred : u.base_pred);
                } else {
                        u.direction_prediction (true);
                }
                // ---------------- target ----------------
                unsigned int set = btb_set (b.address), tag = btb_tag (b.address);
                unsigned int tgt = 0, ft = b.address + 2;  // default: 2-byte short Jcc
                for (int w = 0; w < BTB_WAYS; w++)
                        if (btb[set][w].valid && btb[set][w].tag == tag) {
                                tgt = btb[set][w].target;
                                if (btb[set][w].fallthru) ft = btb[set][w].fallthru;
                                break;
                        }
                bool dir = (b.br_flags & BR_CONDITIONAL) ? u.direction_prediction () : true;
                u.pred_target = dir ? tgt : ft;
                u.target_prediction (u.pred_target);
                return &u;
        }

        void update (branch_update *bu, bool taken, unsigned int target) {
                cpm_update *p = (cpm_update *) bu;
                if (bi.br_flags & BR_CONDITIONAL) {
                        // train bimodal/global predictor
                        core.update (&core.u, taken, target);

                        // train loop predictor
                        loop_entry &e = lp[lp_index (bi.address)];
                        unsigned int tag = lp_tag (bi.address);
                        if (e.valid && e.tag == tag) {
                                // usefulness: only when the two predictors disagree
                                if (p->loop_hit && p->loop_pred != p->base_pred) {
                                        if (p->loop_pred == taken) { if (e.useful < 7) e.useful++; }
                                        else                       { if (e.useful > -8) e.useful--; }
                                }
                                if (taken) {
                                        e.iter++;
                                } else {
                                        if (e.iter == e.trip) { if (e.conf < LP_CONF_MAX) e.conf++; }
                                        else { e.trip = e.iter; e.conf = 0; }
                                        e.iter = 0;
                                }
                        } else if (!taken && p->direction_prediction () != taken) {
                                // allocate on a mispredicted loop exit; a confident
                                // entry is not evicted immediately, it is aged instead
                                if (e.valid && e.conf > 0) { e.conf--; goto lp_done; }
                                e.valid = true; e.tag = tag;
                                e.trip = 0xFFFFFFFF;        // unknown until next exit
                                e.iter = 0; e.conf = 0; e.useful = 0;
                        }
                lp_done: ;
                }
                // train BTB with the actual target (all branch types)
                unsigned int set = btb_set (bi.address), tag = btb_tag (bi.address);
                int way = -1;
                for (int w = 0; w < BTB_WAYS; w++)
                        if (btb[set][w].valid && btb[set][w].tag == tag) { way = w; break; }
                if (way < 0) {
                        way = 0;
                        for (int w = 0; w < BTB_WAYS; w++)
                                if (!btb[set][w].valid) { way = w; goto done; }
                        for (int w = 0; w < BTB_WAYS; w++)
                                if (btb[set][w].lru > btb[set][way].lru) way = w;
                done:
                        btb[set][way].valid = true;
                        btb[set][way].tag = tag;
                        btb[set][way].target = 0;
                        btb[set][way].fallthru = 0;
                }
                if (taken) btb[set][way].target = target;
                else       btb[set][way].fallthru = target;
                btb_touch (set, way);
        }
};
