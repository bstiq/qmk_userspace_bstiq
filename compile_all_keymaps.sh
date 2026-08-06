dos2unix compile_all_keymaps.sh
cd ../bastardkb-qmk
git switch bkb-develop
qmk git-submodule
cd ../qmk_userspace_private
qmk compile -c -kb bastardkb/dilemma/3x5_3_procyon -km vendor
qmk compile -c -kb bastardkb/dilemma/4x6_4_procyon -km vendor
qmk compile -c -kb bastardkb/charybdis/4x6/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/charybdis/3x5/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/charybdis/3x6/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/charybdis/4x6_left/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/charybdis/3x5_left/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/charybdis/3x6_left/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/scylla/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/skeletyl/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/tbkmini/splinktegrated_rev1 -km vendor
qmk compile -c -kb bastardkb/dilemma/3x5_3 -km vendor
qmk compile -c -kb bastardkb/dilemma/4x6_4 -km vendor