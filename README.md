# Pentium M Hybrid Branch Predictor (CBP2 infrastructure)

Author: Kavya Sri Puvvada (COSC 6385, University of Houston)  
Repository: https://github.com/K-a-v-y-a-s-r-i/Branch-Predictor_KSP

Implementation of the `pm_predictor` class in `src/my_predictor.h`: a simplified
Pentium M branch-direction predictor combining a **bimodal predictor** and a
**2-way set-associative tagged global predictor** (average 6.217 MPKI vs 6.306
for the 32K-entry gshare).

**Extra credit (partial):** `cpm_predictor` – an extended, *partial* model of
the Pentium M predictor: bimodal + global + **loop predictor** for direction,
and a 2048-entry 4-way **BTB** for target prediction (average 5.831 direction
MPKI, 6.644 target MPKI). Not modelled: indirect BTB, PIR-based hashing,
return stack; the loop predictor is organised differently from the paper.

**Framework note:** no framework file is modified (only `src/my_predictor.h`
was edited, plus new files). In the provided `src/predictor.h` the getter
`bool target_prediction()` returns `bool`, which truncates every predicted
target to 0/1, so `predict_extra_credit` always reports ~100+ target MPKI.
`src/measure_target.cc` (new) is a copy of `predict_extra_credit.cc` that
reads the predicted target from `cpm_update::pred_target` instead, so target
accuracy can be measured with the framework unchanged.

## Build
```
cd src
make
```
Produces `src/predict` (uses `pm_predictor`) and `src/predict_extra_credit`
(uses `cpm_predictor`). `./run_extra.sh` additionally builds
`src/measure_target` for correct target MPKI.

## Run
```
./run traces        # original csh script (needs csh/tcsh)
./run.sh traces     # bash equivalent, same output
./run_extra.sh traces   # extra credit: direction + target MPKI
```
Prints MPKI per trace and the average.

## Files
- `src/my_predictor.h` – predictor implementation (`pm_predictor`)
- `run.sh` – bash version of the run script
- `results_pm.txt` – MPKI of the hybrid predictor on all 20 traces
- `results_gshare_baseline.txt` – MPKI of the provided gshare for comparison
- `results_bimodal_only.txt` – bimodal-only baseline (`-DPM_NO_GLOBAL`)
- `results_extra_credit.txt` – extended/partial Pentium M (`cpm_predictor`), direction + target
- `src/measure_target.cc` – extra-credit driver that measures targets correctly
- `run_extra.sh` – bash version of `run_extra`
- `REPORT.pdf` – report

## Configuration
Sizes are compile-time parameters in `src/my_predictor.h`
(`PM_WAYS`, `PM_ROW_BITS`, `PM_TAG_BITS`, `PM_HIST_LEN`, `PM_BIM_BITS`), e.g.
```
cd src && make clean && make CXXFLAGS="-O3 -DPM_WAYS=4"
```
`results_pm_4way_2048.txt` – 4-way, 2048-entry global table (Pentium M-like), average MPKI 5.953.
