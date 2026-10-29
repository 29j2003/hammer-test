// sky_presets.cpp - Light presets per sky (see sky_presets.h). Table made from Valve's
// "List of skies" (developer.valvesoftware.com), first example map per sky.

#include <sme/sky_presets.h>

#include <sme/text.h>

namespace sme {

namespace {

constexpr double NONE = SkyPreset::none;

constexpr SkyPreset PRESETS[] = {
	{"sky_borealis01", "Half-Life 2", "0 185 0", -2.0, "245 255 168 40", "120 226 255 10", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_day01_01", "Half-Life 2", "0 165 0", -35.0, "237 218 143 800", "190 201 220 100", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_04", "Half-Life 2", "0 165 0", -55.0, "248 234 205 1000", "194 236 252 175", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_05", "Half-Life 2", "0 165 0", -83.0, "243 224 158 1200", "194 236 252 225", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_06", "Half-Life 2", "0 148 0", -35.0, "240 187 117 1000", "135 172 180 150", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_07", "Half-Life 2", "0 108 0", -35.0, "235 166 71 1000", "135 172 180 150", "-1 -1 -1 1", 0.5, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_08", "Half-Life 2", "0 108 0", -35.0, "235 155 71 1000", "105 142 165 120", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_09", "Half-Life 2", "0 12 0", -45.0, "175 230 239 65", "83 104 130 50", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_01", "Half-Life 2", "0 225 0", -25.0, "254 242 160 400", "172 196 204 80", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_02", "Half-Life 2", "0 225 0", -20.0, "255 244 213 700", "172 196 204 50", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_03", "Half-Life 2", "0 280 0", -45.0, "243 238 224 500", "172 196 204 100", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_04", "Half-Life 2", "0 240 0", -45.0, "243 238 224 400", "172 196 204 50", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_05", "Half-Life 2", "0 135 0", -30.0, "255 255 249 448", "172 196 204 100", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_06", "Half-Life 2", "0 270 0", -20.0, "255 252 230 475", "172 196 204 120", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_07", "Half-Life 2", "0 225 0", -15.0, "255 252 230 500", "183 204 210 140", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_09", "Half-Life 2", "0 200 0", -5.0, "237 232 209 70", "122 140 169 60", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.3, 0.0},
	{"sky_day02_10", "Half-Life 2", "0 135 0", -30.0, "255 255 249 448", "172 196 204 100", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day03_01", "Half-Life 2", "0 165 0", -35.0, "237 218 143 800", "190 201 220 100", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day03_02", "Half-Life 2", "0 140 0", -35.0, "236 218 145 650", "160 184 211 150", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day03_03", "Half-Life 2", "0 120 0", -40.0, "236 218 145 700", "160 184 211 150", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day03_04", "Half-Life 2", "0 53 0", -60.0, "239 207 141 350", "75 96 116 300", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day03_05", "Half-Life 2", "0 82 0", -12.0, "238 200 128 450", "75 96 116 250", "-1 -1 -1 1", 1.0, "-1 -1 -1 1", 0.7, 0.0},
	{"sky_day03_06", "Half-Life 2", "0 280 0", -21.0, "221 112 13 300", "255 255 255 20", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_01_background", "Half-Life 2", "0 165 0", -35.0, "237 218 143 800", "190 201 220 100", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_05_background", "Half-Life 2", "0 165 0", -35.0, "248 233 186 860", "140 167 174 150", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_ep01_00_hdr", "Half-Life 2: Episode One", "0 90 0", -10.0, "237 228 209 400", "75 73 114 100", "237 228 209 350", 1.0, "75 73 114 80", 1.0, 0.0},
	{"sky_ep01_02", "Half-Life 2: Episode One", "0 300 0", -25.0, "219 191 148 500", "207 172 133 100", "242 234 170 500", 1.0, "124 71 46 50", 1.0, 0.0},
	{"sky_ep01_04a", "Half-Life 2: Episode One", "0 310 0", -30.0, "240 217 183 500", "190 143 112 40", "240 217 183 400", 1.0, "190 143 112 40", 1.0, 0.0},
	{"sky_ep01_04", "Half-Life 2: Episode One", "0 300 0", -25.0, "219 191 148 500", "207 172 133 100", "237 207 175 500", 1.0, "181 145 96 120", 1.0, 0.0},
	{"sky_ep01_citadel_int", "Half-Life 2: Episode One", "0 270 0", -85.0, "204 137 36 50", "91 38 48 500", "-1 -1 -1 1", 1.0, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_fog", "Half-Life 2: Episode One", "0 284 0", -70.0, "255 255 255 200", "200 194 183 80", "-1 -1 -1 1", 1.0, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_ep02_01_hdr", "Half-Life 2: Episode Two", "0 145 0", -35.0, "252 231 167 700", "144 175 198 150", "-1 -1 -1 1", 0.6, "-1 -1 -1 1", 0.5, 1.0},
	{"sky_ep02_02_hdr", "Half-Life 2: Episode Two", "0 130 0", -30.0, "240 238 217 650", "144 175 198 280", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 0.7, 1.0},
	{"sky_ep02_03_hdr", "Half-Life 2: Episode Two", "0 114 0", -35.0, "240 238 217 600", "144 175 198 200", "-1 -1 -1 1", 0.5, "-1 -1 -1 1", 0.4, 1.0},
	{"sky_ep02_04_hdr", "Half-Life 2: Episode Two", "0 40 0", -25.0, "242 210 189 700", "144 175 198 145", "-1 -1 -1 1", 0.6, "-1 -1 -1 1", 0.6, 0.0},
	{"sky_ep02_05_hdr", "Half-Life 2: Episode Two", "0 45 0", -35.0, "240 185 130 800", "104 133 162 350", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 0.7, 1.0},
	{"sky_ep02_06_hdr", "Half-Life 2: Episode Two", "0 18 0", -15.0, "240 184 130 750", "70 124 164 200", "-1 -1 -1 1", 0.4, "-1 -1 -1 1", 0.5, 1.0},
	{"sky_ep02_07", "Half-Life 2: Episode Two", "0 122 0", -85.0, "247 251 253 400", "255 255 255 20", "-1 -1 -1 1", 1.0, "-1 -1 -1 1", 1.0, 5.0},
	{"sky_ep02_caves_hdr", "Half-Life 2: Episode Two", "0 180 0", -5.0, "132 158 202 1000", "132 158 202 20", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 0.7, 0.0},
	{"sky_lostcoast_hdr", "Half-Life 2: Lost Coast", "25 345 0", -32.0, "253 236 198 400", "136 155 191 255", "-1 -1 -1 1", 1.0, "-1 -1 -1 1", 1.0, 0.0},
	{"assault", "Counter-Strike: Source", "-57 305 0", -57.0, "249 241 176 850", "151 194 204 80", nullptr, NONE, nullptr, NONE, NONE},
	{"cx", "Counter-Strike: Source", "0 27 0", -65.0, "255 242 192 600", "145 172 189 300", nullptr, NONE, nullptr, NONE, NONE},
	{"de_cobble", "Counter-Strike: Source", "0 330 0", -40.0, "240 252 255 700", "146 149 155 300", nullptr, NONE, nullptr, NONE, NONE},
	{"de_cobble_hdr", "Counter-Strike: Source", "0 26 0", -60.0, "252 230 197 550", "136 177 210 100", nullptr, NONE, nullptr, NONE, NONE},
	{"de_piranesi", "Counter-Strike: Source", "0 150 0", -50.0, "255 248 192 450", "152 160 168 300", nullptr, NONE, nullptr, NONE, NONE},
	{"hav", "Counter-Strike: Source", "0 300 0", -12.0, "251 215 140 800", "143 176 214 300", nullptr, NONE, nullptr, NONE, NONE},
	{"italy", "Counter-Strike: Source", "0 165 0", -45.0, "255 232 208 600", "183 201 206 200", nullptr, NONE, nullptr, NONE, NONE},
	{"jungle", "Counter-Strike: Source", "0 287 0", -60.0, "248 244 237 350", "190 209 216 150", nullptr, NONE, nullptr, NONE, NONE},
	{"militia_hdr", "Counter-Strike: Source", "0 298 0", -60.0, "235 222 177 600", "75 92 118 400", nullptr, NONE, nullptr, NONE, NONE},
	{"office", "Counter-Strike: Source", "0 17 0", -60.0, "255 249 225 325", "121 152 185 400", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_dust", "Counter-Strike: Source", "0 43 0", -60.0, "233 225 201 700", "241 228 190 500", nullptr, NONE, nullptr, NONE, NONE},
	{"tides", "Counter-Strike: Source", "0 51 0", -60.0, "242 224 183 850", "105 150 186 800", nullptr, NONE, nullptr, NONE, NONE},
	{"train", "Counter-Strike: Source", "0 120 0", -57.0, "255 250 234 700", "98 129 179 500", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_c17_05", "Counter-Strike: Source", "0 0 0", -15.0, "38 64 79 50", "38 64 79 10", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_dod_01_hdr", "Day of Defeat: Source", "0 165 0", -40.0, "250 241 192 600", "209 227 237 200", "250 241 192 500", NONE, "209 227 237 200", NONE, NONE},
	{"sky_dod_02_hdr", "Day of Defeat: Source", "0 165 0", -40.0, "242 240 219 450", "149 171 181 250", "234 230 196 370", NONE, "149 171 181 300", NONE, NONE},
	{"sky_dod_03_hdr", "Day of Defeat: Source", "0 32 0", -50.0, "250 241 192 600", "209 227 237 200", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, NONE},
	{"sky_dod_04_hdr", "Day of Defeat: Source", "0 60 0", -50.0, "250 241 192 600", "149 171 181 250", "250 241 192 425", NONE, "149 171 181 200", NONE, NONE},
	{"sky_dod_06_hdr", "Day of Defeat: Source", "0 113 0", -22.0, "255 255 164 600", "203 216 235 200", "255 255 164 500", NONE, "203 216 235 200", NONE, NONE},
	{"sky_dod_08_hdr", "Day of Defeat: Source", nullptr, NONE, "197 205 214 750", "136 175 204 280", "197 205 214 750", NONE, "-1 -1 -1 1", NONE, NONE},
	{"sky_dod_09_hdr", "Day of Defeat: Source", nullptr, NONE, "128 165 202 165", "51 86 125 650", "128 165 202 50", NONE, "51 86 125 325", NONE, NONE},
	{"sky_dod_10_hdr", "Day of Defeat: Source", nullptr, NONE, "185 200 206 450", "111 140 153 600", "185 200 206 150", NONE, "111 140 153 200", NONE, NONE},
	{"sky_alpinestorm_01", "Team Fortress 2", "0 132 0", -66.0, "61 72 90 275", "34 38 55 175", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 5.0},
	{"sky_badlands_01", "Team Fortress 2", "0 210 0", -30.0, "251 201 155 250", "153 159 187 170", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.0},
	{"sky_dustbowl_01", "Team Fortress 2", "0 30 0", -25.0, "254 210 152 800", "151 135 180 420", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 5.0},
	{"sky_goldrush_01", "Team Fortress 2", "0 108 0", -35.0, "254 226 186 600", "118 114 139 350", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 5.0},
	{"sky_granary_01", "Team Fortress 2", "0 130 0", -15.0, "255 226 196 500", "116 113 149 300", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.0},
	{"sky_gravel_01", "Team Fortress 2", "0 167 0", -32.0, "250 216 146 850", "106 134 174 500", "250 216 149 650", NONE, "106 134 174 380", NONE, 0.0},
	{"sky_halloween", "Team Fortress 2", "0 105 0", -30.0, "149 164 181 190", "79 94 128 90", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 10.0},
	{"sky_halloween_night_01", "Team Fortress 2", "0 131 0", -35.0, "149 164 181 190", "79 94 128 90", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 10.0},
	{"sky_halloween_night2014_01", "Team Fortress 2", "0 118 0", -35.0, "84 85 112 160", "61 67 80 55", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 5.0},
	{"sky_harvest_01", "Team Fortress 2", "0 300 0", -20.0, "238 218 181 250", "224 188 122 250", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.0},
	{"sky_harvest_night_01", "Team Fortress 2", "0 285 0", -20.0, "133 101 159 275", "114 100 151 57", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 3.0},
	{"sky_hydro_01", "Team Fortress 2", "0 51 0", -37.0, "244 215 193 750", "156 178 255 250", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.5},
	{"sky_island_01", "Team Fortress 2", "0 248 0", -65.0, "252 239 209 500", "142 154 185 200", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.0},
	{"sky_morningsnow_01", "Team Fortress 2", "0 131 0", -25.0, "251 199 174 450", "196 174 210 170", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 5.0},
	{"sky_night_01", "Team Fortress 2", "0 265 0", -35.0, "34 38 45 250", "26 29 34 250", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 5.0},
	{"sky_nightfall_01", "Team Fortress 2", "0 280 0", -60.0, "201 209 220 20", "90 69 101 15", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 10.0},
	{"sky_stormfront_01", "Team Fortress 2", "0 131 0", -65.0, "253 243 208 50", "188 174 210 500", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 15.0},
	{"sky_tf2_04", "Team Fortress 2", "0 145 0", -45.0, "216 207 194 700", "200 202 230 60", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.5},
	{"sky_trainyard_01", "Team Fortress 2", "0 300 0", -65.0, "253 243 208 600", "124 138 203 500", "253 243 208 800", NONE, "124 138 203 650", NONE, 0.0},
	{"sky_upward", "Team Fortress 2", "0 325 0", -55.0, "253 242 208 1000", "159 174 185 450", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.5},
	{"sky_well_01", "Team Fortress 2", "0 300 0", -35.0, "248 197 139 400", "142 154 182 200", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.0},
	{"sky_l4d_urban01_hdr", "Left 4 Dead", "0 115 0", -13.0, "112 159 186 50", "52 79 99 5", nullptr, NONE, nullptr, NONE, NONE},
	{"test_moon_hdr", "Left 4 Dead", "0 115 0", -13.0, "112 159 186 50", "52 79 99 15", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_day01_09_hdr", "Left 4 Dead", "0 150 0", -40.0, "75 115 126 60", "52 100 116 10", nullptr, NONE, nullptr, NONE, NONE},
	{"urbannightburning_hdr", "Left 4 Dead", "0 225 0", -15.0, "108 69 36 100", "60 78 91 15", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_rural02_hdr", "Left 4 Dead", "0 220 0", -8.0, "225 187 174 40", "107 131 165 10", nullptr, NONE, nullptr, NONE, NONE},
	{"river_hdr", "Left 4 Dead", "0 18 0", -15.0, "185 157 115 60", "133 152 167 15", nullptr, NONE, nullptr, NONE, NONE},
	{"docks_hdr", "Left 4 Dead", "0 18 0", -15.0, "185 157 115 60", "133 152 167 15", nullptr, NONE, nullptr, NONE, NONE},
	{"highrise_hdr", "Left 4 Dead", "0 18 0", -15.0, "185 157 115 60", "133 152 167 15", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_c1_1_hdr", "Left 4 Dead 2", "0 30 0", -14.0, "228 215 192 400", "171 206 220 50", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_coldstream_1_hdr", "Left 4 Dead 2", "0 30 0", -14.0, "228 215 192 400", "171 206 220 50", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_c1_2_hdr", "Left 4 Dead 2", "0 5 0", -30.0, "236 199 149 200", "160 196 207 6", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_c2m1_hdr", "Left 4 Dead 2", "0 100 0", -15.0, "220 170 114 2", "106 160 193 3", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_night02_hdr", "Left 4 Dead 2", "0 175 0", -20.0, "182 163 118 15", "133 152 167 5", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_predawn02_hdr", "Left 4 Dead 2", "0 186 0", -7.0, "230 186 138 200", "177 201 209 6", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_c4m1_hdr", "Left 4 Dead 2", "0 345 0", -16.0, "219 153 100 120", "202 214 227 15", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_c4m4_hdr", "Left 4 Dead 2", "0 315 0", -15.0, "200 225 230 10", "79 108 104 5", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_c5_1_hdr", "Left 4 Dead 2", "0 150 0", -12.0, "209 154 109 1000", "202 214 227 25", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_coldstream_2_hdr", "Left 4 Dead 2", "0 150 0", -12.0, "209 154 109 1000", "202 214 227 25", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_l4d_c6m1_hdr", "Left 4 Dead 2", "0 48 0", -45.0, "238 207 162 6", "137 125 147 15", nullptr, NONE, nullptr, NONE, NONE},
	{"blacksky", "Alien Swarm", "0 146 0", -45.0, "67 82 120 75", "56 73 99 40", "-1 -1 -1 1", 1.0, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day02_07_hdr", "Alien Swarm", "0 146 0", -45.0, "67 82 120 75", "56 73 99 40", "-1 -1 -1 1", 1.0, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_wasteland02", "Alien Swarm", "0 37 0", -15.0, "255 244 213 512", "172 196 204 50", "-1 -1 -1 1", 1.0, "-1 -1 -1 1", 1.0, 0.0},
	{"sky_day01_05_hdr", "Portal", "0 122 0", -45.0, "249 245 202 900", "192 221 226 200", "-1 -1 -1 1", 0.7, "-1 -1 -1 1", 0.7, 0.0},
	{"sky_white", "Portal 2", "0 280 0", -85.0, "133 156 182 300", "133 156 182 300", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 10.0},
	{"sky_black_nofog", "Portal 2", "0 120 0", -15.0, "255 255 255 0", "124 124 124 20", "-1 -1 -1 1", NONE, "-1 -1 -1 1", NONE, 0.0},
	{"cs_baggage_skybox_", "Counter-Strike: Global Offensive", "0 165 0", -45.0, "253 230 240 600", "226 233 239 200", nullptr, NONE, nullptr, NONE, NONE},
	{"cs_tibet", "Counter-Strike: Global Offensive", "0 153 0", -35.0, "239 235 186 475", "142 166 176 225", nullptr, NONE, nullptr, NONE, NONE},
	{"vietnam", "Counter-Strike: Global Offensive", "0 33 0", -36.0, "239 228 186 600", "199 224 233 350", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_lunacy", "Counter-Strike: Global Offensive", "0 65 0", -60.0, "255 240 206 710", "101 139 182 550", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_cs15_daylight01_hdr", "Counter-Strike: Global Offensive", "0 117 0", -45.0, "247 232 202 600", "189 208 223 300", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_cs15_daylight02_hdr", "Counter-Strike: Global Offensive", "0 321 0", -35.0, "247 232 202 600", "165 187 203 190", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_cs15_daylight03_hdr", "Counter-Strike: Global Offensive", "0 285 0", -45.0, "249 238 200 900", "160 172 201 650", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_cs15_daylight04_hdr", "Counter-Strike: Global Offensive", "0 117 0", -47.0, "250 240 220 600", "179 200 202 200", nullptr, NONE, nullptr, NONE, NONE},
	{"nukeblank", "Counter-Strike: Global Offensive", "0 218 0", -63.0, "255 238 198 700", "114 149 188 520", nullptr, NONE, nullptr, NONE, NONE},
	{"dustblank", "Counter-Strike: Global Offensive", "35 321 0", -35.0, "247 232 202 620", "173 193 207 420", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_venice", "Counter-Strike: Global Offensive", "0 140 0", -50.0, "232 204 130 640", "140 173 234 400", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_csgo_cloudy01", "Counter-Strike: Global Offensive", "0 341 0", -35.0, "201 211 224 45", "68 72 115 100", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_csgo_night02", "Counter-Strike: Global Offensive", "0 341 0", -35.0, "201 211 224 45", "68 72 115 100", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_csgo_night02b", "Counter-Strike: Global Offensive", "0 341 0", -35.0, "201 211 224 45", "68 72 115 100", nullptr, NONE, nullptr, NONE, NONE},
	{"vertigoblue_hdr", "Counter-Strike: Global Offensive", "0 65 0", -72.0, "245 241 214 400", "153 190 221 250", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_hr_aztec", "Counter-Strike: Global Offensive", "0 43 0", -50.0, "247 232 202 640", "202 216 219 420", nullptr, NONE, nullptr, NONE, NONE},
	{"alien1", "Half-Life: Source", nullptr, NONE, "112 188 173 1000", "84 140 156 250", nullptr, NONE, nullptr, NONE, NONE},
	{"black", "Half-Life: Source", nullptr, NONE, "62 202 150 800", "84 140 156 150", nullptr, NONE, nullptr, NONE, NONE},
	{"desert", "Half-Life: Source", nullptr, NONE, "255 224 160 5000", "255 255 255 400", nullptr, NONE, nullptr, NONE, NONE},
	{"neb6", "Half-Life: Source", nullptr, NONE, "156 130 107 1200", "255 255 255 50", nullptr, NONE, nullptr, NONE, NONE},
	{"neb7", "Half-Life: Source", nullptr, NONE, "70 150 120 500", "255 255 255 200", nullptr, NONE, nullptr, NONE, NONE},
	{"night", "Half-Life: Source", nullptr, NONE, "70 119 200 200", "255 255 255 20", nullptr, NONE, nullptr, NONE, NONE},
	{"sky_dawn01", "Half-Life: Source", "0 300 0", -20.0, "210 177 100 900", "187 238 236 30", nullptr, NONE, nullptr, NONE, NONE},
	{"xen9", "Half-Life: Source", nullptr, NONE, "202 236 64 600", "255 255 255 0", nullptr, NONE, nullptr, NONE, NONE},
};

bool given(double v) { return v != NONE; }

std::string number(double v) { return formatNumber(v); }

/// "pitch yaw roll" for entities that point the usual way (shadow_control, env_global_light):
/// light_environment's pitch is negative for light from above, theirs positive.
std::string downAngles(const SkyPreset& preset, const Vec3& angles) {
	const double pitch = given(preset.pitch) ? preset.pitch : angles.x;
	return number(-pitch) + " " + number(angles.y) + " 0";
}

std::vector<Entity*> ofClass(MapDocument& doc, std::string_view classname) {
	std::vector<Entity*> out;
	for (auto& entity : doc.entities) {
		if (iequals(entity.classname(), classname)) out.push_back(&entity);
	}
	return out;
}

} // namespace

std::span<const SkyPreset> skyPresets() {
	return PRESETS;
}

const SkyPreset* findSkyPreset(std::string_view skyName) {
	const std::string name = toLower(skyName);
	const auto find = [](std::string_view wanted) -> const SkyPreset* {
		for (const auto& preset : PRESETS) {
			if (iequals(preset.name, wanted)) return &preset;
		}
		return nullptr;
	};
	if (name.empty()) return nullptr;
	if (const auto* preset = find(name)) return preset;
	if (name.ends_with("_hdr")) return find(name.substr(0, name.size() - 4));
	return find(name + "_hdr");
}

Change applySky(MapDocument& doc, const std::string& skyName, const Vec3& place) {
	Transaction t(doc, "Sky " + skyName);
	applySky(doc, t, skyName, place);
	return t.finish();
}

void applySky(MapDocument& doc, Transaction& t, const std::string& skyName, const Vec3& place) {
	t.touchWorld();
	doc.world.set("skyname", skyName);
	const SkyPreset* preset = findSkyPreset(skyName);
	if (!preset) {
		return;
	}
	Vec3 angles;
	const bool hasAngles = preset->angles && parseVec3(preset->angles, angles);

	auto lights = ofClass(doc, "light_environment");
	if (lights.empty()) {
		Vec3 at = place;
		for (const auto& entity : doc.entities) {
			if (iequals(entity.classname(), "info_player_start")) {
				at = entity.origin() + Vec3{0, 0, 64};
				break;
			}
		}
		Entity entity;
		entity.id = doc.newEntityId();
		entity.set("classname", "light_environment");
		entity.setOrigin(at);
		t.touchEntity(entity.id);
		doc.entities.push_back(std::move(entity));
		lights = ofClass(doc, "light_environment");
	}
	for (Entity* light : lights) {
		t.touchEntity(light->id);
		if (hasAngles) light->set("angles", preset->angles);
		if (given(preset->pitch)) light->set("pitch", number(preset->pitch));
		light->set("_light", preset->light);
		light->set("_ambient", preset->ambient);
		if (preset->lightHdr) light->set("_lightHDR", preset->lightHdr);
		if (given(preset->lightScaleHdr)) light->set("_lightscaleHDR", number(preset->lightScaleHdr));
		if (preset->ambientHdr) light->set("_ambientHDR", preset->ambientHdr);
		if (given(preset->ambientScaleHdr)) light->set("_AmbientScaleHDR", number(preset->ambientScaleHdr));
		if (given(preset->sunSpread)) light->set("SunSpreadAngle", number(preset->sunSpread));
	}
	if (!hasAngles) {
		return;   // only colours known: the direction stays as the mapper set it
	}
	for (Entity* sun : ofClass(doc, "env_sun")) {
		t.touchEntity(sun->id);
		sun->set("angles", preset->angles);
		if (given(preset->pitch)) sun->set("pitch", number(preset->pitch));
	}
	for (std::string_view classname : {"shadow_control", "env_global_light"}) {
		for (Entity* entity : ofClass(doc, classname)) {
			t.touchEntity(entity->id);
			entity->set("angles", downAngles(*preset, angles));
		}
	}
}

} // namespace sme
