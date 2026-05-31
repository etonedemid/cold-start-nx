// ─── ui.cpp ─── Immediate-mode UI implementation ────────────────────────────
#include "ui.h"
#include "assets.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace UI {

// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⠀⡄⣐⣥⣬⣯⣴⣶⣻⣾⣷⣿⢿⣶⣦⣶⣭⣠⡆⣠⠀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡀⣀⣰⣴⣯⣿⣻⢿⣻⣟⣾⡽⣷⢯⣷⣟⣾⡽⣯⣷⢯⡿⣽⣻⣽⡷⣿⣤⣃⡂⠄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣸⢿⣟⡿⣽⣳⣯⣟⡿⣽⡾⣯⢿⣽⣟⡷⣯⡿⣽⣟⣾⢿⣽⢿⣽⣞⣿⣳⢿⣻⢿⣳⣆⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⣽⣟⣯⣿⣻⣽⣳⣟⣿⣽⣻⣽⣟⡷⣯⡿⣯⢿⣽⢾⣯⢿⣞⡿⣾⡽⣞⡿⣯⣟⡿⣽⣻⣞⣻⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣴⣿⢯⣷⢯⣷⢿⣽⣾⣻⢾⣽⣳⣯⢿⣽⣻⡽⣟⣾⣻⣞⡿⣾⣽⣳⣟⣯⣟⡷⣯⢿⣽⣳⡟⡄⠀⠀⠀⢤⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠰⣿⡾⣿⣽⣻⡽⣟⣾⣳⢿⣯⣟⡷⣯⢿⣞⣷⣻⡽⣞⡷⣫⣽⢳⡭⣗⢯⡞⣭⡟⣽⣫⣞⣷⠹⠀⠀⠀⠠⠆⠋⠙⠠⡄⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⡄⡀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣺⣿⣽⢷⣯⢿⣽⢿⡽⣯⢿⡾⣽⣻⣽⢻⡞⣧⢏⡷⣫⢞⡵⣋⠷⣙⢮⢳⡹⢶⡙⣧⠳⡞⢧⠒⠀⠀⠀⡜⠈⠁⠂⠄⡀⠁⠃⠆⡄⣂⠀⢀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⠀⣄⣠⣤⣶⣾⣿⣿⠀⡘
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣘⡿⣷⣯⢿⡾⣿⡽⣯⢿⡽⣯⣟⠷⣏⣞⢯⡞⡵⣋⢞⡱⢎⠶⣉⠞⡱⢊⠥⣃⠧⡙⢆⠯⣙⠞⠀⠀⠀⠐⢀⡉⠀⠐⠀⠠⠁⡈⠄⠠⠁⢊⠱⠒⡎⡤⢤⣄⣀⣀⣄⡤⣤⣤⣴⣷⣶⣿⣿⣿⣿⣿⣿⣿⣿⠇⢀⡙
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣽⣿⣻⢾⡿⣽⢷⣻⡽⣯⣟⢷⡹⢯⡝⣎⠳⣜⡱⣉⠎⠴⡉⢆⠡⢊⠑⠌⠒⠄⠊⠜⠨⡘⢥⡉⠀⠀⠀⠋⠠⠀⠀⠈⠀⠐⠀⠠⠀⡁⠌⢀⠂⡑⠄⡃⢧⠸⣜⡹⢮⣝⣳⢯⣟⡾⣟⣿⡿⣿⣿⣿⣿⣿⡟⠀⣬⠁
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣰⣯⣷⢿⣯⣟⣯⡿⣽⣻⢧⡟⣾⢹⢣⡝⢬⠓⡤⢃⠤⢉⠂⡐⠀⠂⠀⠈⠀⠈⠀⠁⠈⠐⢈⣮⠀⠀⠀⣈⠠⠄⠀⠀⠀⠈⠀⠀⠁⡀⠠⠀⠂⠠⠐⠠⠑⢂⠓⡬⢱⡓⢮⣳⣛⡾⣽⣻⢯⣿⣿⢿⣿⣿⣿⠁⡰⠆⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠠⣿⣽⡾⣟⣾⡽⣾⣽⣳⢯⡞⡽⢲⡍⡖⡩⢆⠩⠐⡀⠂⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢮⠄⠀⠀⢠⠄⠐⠀⠀⠀⠀⠀⠀⠀⠀⢀⠀⠄⠁⡐⠠⠁⠂⠌⠂⠥⣃⠞⣥⠳⣝⣞⣳⢯⣿⣻⣾⣿⣿⣿⡏⣄⠻⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢺⣿⣳⢿⣯⢷⣻⢷⣏⡞⣧⢯⡱⢣⢚⠰⢁⠂⠄⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠐⠖⠀⠀⠀⡤⠀⠂⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠄⠀⠄⠂⠁⠄⡉⠰⣀⠫⡔⣫⠞⡼⣭⢟⡾⣽⡷⣿⣻⣿⠰⣎⠃⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣈⣿⡷⣟⡿⣞⣯⣟⡯⣞⡽⣒⠧⣃⠇⡊⠔⠂⠈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣸⠃⠀⠀⠰⠀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠄⠀⠂⢁⠠⢀⠁⠄⢣⠘⡴⢋⢷⡹⢾⣹⢷⣻⣟⣿⠇⡾⡈⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣼⣻⣽⢿⣽⣻⣞⡷⡽⢮⠵⣩⠖⣡⠊⡐⠠⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⡏⠀⠀⢀⢃⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠐⠀⠠⠀⠂⢈⠐⠠⣉⠲⣩⠖⡽⣣⢟⣯⢷⣻⡟⣸⢧⠁⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⣟⣯⣟⡿⣞⣷⡻⣞⡽⣋⠞⡥⠚⡄⠒⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣼⠀⠀⠀⡈⡉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⠄⠁⠠⢈⠐⡀⢃⢆⠻⡜⣵⢫⣞⣯⢿⣁⡟⠆⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠠⣞⣿⣽⢾⣻⣽⠾⣝⢧⣛⣥⡿⣴⣯⡴⠭⠾⠵⣧⣶⣶⡶⣤⣄⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⡅⠀⠀⢠⢰⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⠀⠠⠈⠀⠄⠂⠄⠣⢌⠣⡝⣬⢳⡽⣺⡏⣼⠛⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢲⣿⣻⣞⡿⣽⢞⠟⠻⠋⠛⠁⠉⠉⠀⠀⠀⠀⠀⠀⠈⠉⠉⠙⠛⠟⢿⣶⢦⣀⠀⠀⠀⠀⠀⠾⠀⠀⠀⡄⠆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⡀⠡⠐⠈⠠⢁⢊⡱⡙⢦⡻⣜⡷⢦⡟⠃⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠻⡶⠟⠘⠉⠁⠀⠀⠀⠀⠀⠀⢀⡀⠀⠀⠀⠀⢀⣀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠻⠿⣦⣄⠀⣘⠃⠀⠀⠰⠸⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠐⠀⠠⠀⡁⢂⠐⢢⠰⡙⢦⡝⣮⠗⣾⡉⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⢠⣀⡶⢤⠜⠣⠄⠂⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⠐⠀⠀⠀⠀⠀⠀⠀⠀⠙⠛⠍⠀⠀⠀⢃⠃⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠄⠂⠁⠠⠐⠀⠌⣀⠣⡙⢦⡝⣞⢳⢧⠁⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⢀⣀⣄⣶⣼⢷⡛⢯⡱⣉⠆⡘⠠⢀⠀⠄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡜⣈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠠⠀⢁⠠⠁⠂⠤⢡⡙⢦⣹⡍⠺⠆⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⣜⣿⣻⠽⣎⢷⡹⢆⡳⠰⢌⡐⠁⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⠻⢷⣦⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠐⠀⠐⠀⠠⢈⠐⠠⢃⡜⢦⠯⢀⠛⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⢠⣞⡾⣭⢿⡹⢮⣕⢫⡒⣍⠢⠄⠡⠀⠀⠄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⠁⠀⠀⠀⠀⠀⠀⠈⠛⠷⣦⣄⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠐⠈⢀⠁⠄⢈⣐⣡⣬⣶⡟⠟⠁⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠶⣾⡽⣯⣳⢏⡷⢎⢧⡓⡌⢆⠱⢀⠁⠀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡆⠀⠀⠠⡲⠂⠀⢀⠀⠀⠀⠀⠉⠛⠻⢶⡦⣤⣄⣀⣀⣀⠀⠀⠀⢀⣀⣀⣀⣈⣤⣤⡶⢶⠿⠻⠋⠋⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠘⣽⣷⣻⣗⣯⢻⡜⣯⠲⣍⡜⡌⢒⠠⠈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠰⠀⠀⠀⠂⡄⠀⠀⠄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠁⠈⠛⠉⠋⠉⠉⠉⠉⠉⠛⠋⠀⠉⠀⠀⠀⠀⠀⡀⢀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⣛⣿⣾⣳⣟⡾⣭⣛⢶⡛⡴⣊⠜⡄⢃⠌⠀⠄⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⠃⠀⠀⡘⠰⠀⠀⠀⠀⠀⠀⠀⠀⠈⠐⠀⠄⢀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣀⠀⠤⠀⠐⣀⠍⠔⠊⠉⠁⠦⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⡸⣽⣿⣞⡷⣯⢿⡵⣏⡾⣱⢧⡹⡘⡌⠆⡌⠐⡀⠠⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡜⠀⠀⢀⠁⠇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠉⠉⠉⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡈⡘⠄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⢠⣹⣿⡿⣾⢿⣽⣻⣞⢷⣹⢳⡎⡵⣙⠜⡒⢌⡐⠠⠀⠀⠄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⠁⠀⠀⡈⡘⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⢁⠳⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⢦⣽⣿⣿⢿⣻⣾⣳⢯⣯⢗⡯⣞⡵⣩⠞⡱⢢⠘⡄⢁⠂⠀⠠⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠆⠀⠀⠠⢠⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡄⡄⠂⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠘⣶⣿⣿⣿⡿⣟⣷⣟⡿⣞⣯⢷⡹⢮⣕⡫⣕⢣⢣⠘⠤⡈⠐⡀⠠⠀⠀⠀⠀⠀⠀⠀⠀⠀⡸⠀⠀⠀⠂⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀etonedemid⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠰⠰⡈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⢀⡻⣼⣿⣿⣿⣿⣿⣻⣾⢿⡽⣞⣯⢟⣳⢎⡷⢬⠳⣌⡹⠤⣁⠃⡄⠠⠀⠁⡀⠄⠀⠀⠀⠀⢀⠃⠀⠀⠀⠲⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠃⢦⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⣘⣳⣿⣿⣿⣿⣿⣽⣿⣽⡿⣽⣻⣞⣯⢟⣮⡝⣏⠷⣌⢧⠓⣌⠲⡈⠅⡌⠠⢀⠀⠄⠂⠀⢀⡝⠀⠀⠀⠀⠣⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡘⡘⠄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⢠⣣⣿⣿⣿⣿⣿⣿⣿⣟⣯⣿⣟⣷⣻⣞⡿⣮⡽⣞⡽⣺⢬⡛⣬⢣⡙⠴⣈⠅⢢⠐⠠⠀⡌⢠⠁⠀⠀⢀⡈⠆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀..⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡀⢡⢁⢳⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⣤⢳⣿⣿⣿⣿⣿⣿⣿⣿⣿⣟⣯⣿⢷⣯⣟⣷⣻⡵⢯⣳⢏⡾⣡⢧⡙⢶⡡⢎⠥⣊⠅⢣⡔⠖⠀⠀⠀⠄⠐⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⠐⠠⡌⡌⠆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⡼⢠⣿⣿⣿⣿⣿⡿⢿⣛⣿⣭⣿⣭⣯⠿⠾⠽⢾⣷⣿⣯⣷⣻⣼⢧⢯⡝⡶⣙⢎⠶⣡⠞⣥⠘⠂⠀⠀⠠⠀⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠄⠂⡌⢰⢠⡘⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⡳⢹⡿⣟⣯⡽⠶⠛⠛⠉⠉⠀⠈⠀⠀⠀⠀⠀⠀⠀⠀⠁⠈⠋⠙⠻⠿⣾⣷⣹⣎⢷⡱⣞⢆⡏⠀⠀⠀⠂⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠠⠈⡄⠣⠌⠆⢦⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠆⠟⠋⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉⠛⢿⣾⣵⡛⣌⠁⠀⠀⡈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠄⡁⢢⠑⡌⡱⠘⠰⡌⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠙⠃⠉⠀⠀⢀⠀⠠⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⠀⢂⢁⠒⡌⢢⠱⣌⢁⠃⢳⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠆⣰⠂⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡀⠄⠂⠤⢘⠠⣊⠴⣈⢇⠳⡌⡌⡈⠆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠉⠝⡲⣄⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⠀⠠⠐⠠⢁⠰⡈⠜⣠⠣⡑⢆⠳⣌⠮⠕⣖⠢⠘⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠐⠩⢗⡲⢤⣀⡂⠤⠀⡄⠐⡀⢂⢁⠂⡌⠰⣁⠣⢌⢆⡱⠩⠔⠣⢙⡊⡭⣔⢮⠽⠆⠃⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⡊⠰⠏⢲⡖⣦⣠⠥⣬⢤⠬⣡⠤⡩⢄⣤⣒⢦⡙⠣⠏⠘⠁⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀

const TextCache::Entry& TextCache::get(const char* text, int size) {
    static const Entry empty{};
    if (!renderer_ || !text || text[0] == '\0') return empty;

    Key key{text, size};
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.frameUsed = frame_;
        return it->second;
    }

    // Evict if cache is too large
    if (cache_.size() >= MAX_ENTRIES) evict(300);

    TTF_Font* f = Assets::instance().font(size);
    if (!f) return empty;

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surf = TTF_RenderText_Blended(f, text, white);
    if (!surf) return empty;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    if (!tex) { SDL_FreeSurface(surf); return empty; }

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    Entry entry;
    entry.texture   = tex;
    entry.width     = surf->w;
    entry.height    = surf->h;
    entry.frameUsed = frame_;
    SDL_FreeSurface(surf);

    auto [ins, _] = cache_.emplace(key, entry);
    return ins->second;
}

void TextCache::evict(uint32_t maxAge) {
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (frame_ - it->second.frameUsed > maxAge) {
            SDL_DestroyTexture(it->second.texture);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void TextCache::clear() {
    for (auto& [k, e] : cache_) {
        if (e.texture) SDL_DestroyTexture(e.texture);
    }
    cache_.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Input Glyphs
// ═════════════════════════════════════════════════════════════════════════════

const char* glyphLabel(Action action, bool gamepad) {
#ifdef __SWITCH__
    // Nintendo Switch glyphs (matches Joy-Con / Pro Controller layout)
    switch (action) {
        case Action::Confirm:  return "A";
        case Action::Back:     return "B";
        case Action::Left:     return "\xE2\x97\x80";   // ◀
        case Action::Right:    return "\xE2\x96\xB6";   // ▶
        case Action::Navigate: return "D-Pad";
        case Action::Pause:    return "+";
        case Action::Tab:      return "Y";
        case Action::Bomb:     return "X";
    }
#else
    if (gamepad) {
        switch (action) {
            case Action::Confirm:  return "A";
            case Action::Back:     return "B";
            case Action::Left:     return "LB";
            case Action::Right:    return "RB";
            case Action::Navigate: return "D-Pad";
            case Action::Pause:    return "Start";
            case Action::Tab:      return "Y";
            case Action::Bomb:     return "X";
        }
    } else {
        switch (action) {
            case Action::Confirm:  return "Enter";
            case Action::Back:     return "Esc";
            case Action::Left:     return "<-";
            case Action::Right:    return "->";
            case Action::Navigate: return "Arrows";
            case Action::Pause:    return "Esc";
            case Action::Tab:      return "Tab";
            case Action::Bomb:     return "Q";
        }
    }
#endif
    return "?";
}

std::string buildHintBar(const HintPair* pairs, int count, bool gamepad) {
    std::string result;
    for (int i = 0; i < count; i++) {
        if (i > 0) result += "     ";
        result += "[";
        result += glyphLabel(pairs[i].action, gamepad);
        result += "] ";
        result += pairs[i].desc;
    }
    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Context
// ═════════════════════════════════════════════════════════════════════════════

void Context::init(SDL_Renderer* r) {
    renderer = r;
    textCache.init(r);
    std::memset(itemAnim, 0, sizeof(itemAnim));
}

void Context::beginFrame(float frameDt, bool gamepad) {
    dt = frameDt;
    usingGamepad = gamepad;
    buttonFired = false;
    mouseWheelY = 0;
    textCache.beginFrame();

    // Save previous frame hover for click-through in handleInput
    prevHoveredItem = hoveredItem;

    // Reset per-frame hit-test
    hoveredItem = -1;
    clickedItem = -1;

    // Get mouse state (SDL logical coordinates thanks to RenderSetLogicalSize)
    int rawX, rawY;
    Uint32 buttons = SDL_GetMouseState(&rawX, &rawY);
    // Convert to logical coords via SDL renderer mapping
    float fx, fy;
    SDL_RenderWindowToLogical(renderer, rawX, rawY, &fx, &fy);
    mouseX = (int)fx;
    mouseY = (int)fy;

    bool wasDown = mouseDown;
    mouseDown = (buttons & SDL_BUTTON_LMASK) != 0;
    mouseClicked  = mouseDown && !wasDown;
    mouseReleased = !mouseDown && wasDown;

    // Suppress clicks for N frames after any button fires to prevent double-fire
    if (clickCooldownFrames > 0) {
        --clickCooldownFrames;
        mouseClicked = false;
    }

    // Evict old text cache entries occasionally (every ~10 seconds)
    static int evictCounter = 0;
    if (++evictCounter > 600) { textCache.evict(600); evictCounter = 0; }
}

void Context::endFrame() {
    // nothing for now
}

void Context::shutdown() {
    textCache.clear();
}

// ─── Drawing Helpers ────────────────────────────────────────────────────────

void Context::drawText(const char* text, int x, int y, int size, SDL_Color color) {
    const auto& e = textCache.get(text, size);
    if (!e.texture) return;
    SDL_SetTextureColorMod(e.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(e.texture, color.a);
    SDL_Rect dst = {x, y, e.width, e.height};
    SDL_RenderCopy(renderer, e.texture, nullptr, &dst);
}

void Context::drawTextCentered(const char* text, int y, int size, SDL_Color color) {
    const auto& e = textCache.get(text, size);
    if (!e.texture) return;
    SDL_SetTextureColorMod(e.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(e.texture, color.a);
    SDL_Rect dst = {SCREEN_W / 2 - e.width / 2, y, e.width, e.height};
    SDL_RenderCopy(renderer, e.texture, nullptr, &dst);
}

void Context::drawTextRight(const char* text, int x, int y, int size, SDL_Color color) {
    const auto& e = textCache.get(text, size);
    if (!e.texture) return;
    SDL_SetTextureColorMod(e.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(e.texture, color.a);
    SDL_Rect dst = {x - e.width, y, e.width, e.height};
    SDL_RenderCopy(renderer, e.texture, nullptr, &dst);
}

int Context::textWidth(const char* text, int size) {
    const auto& e = textCache.get(text, size);
    return e.width;
}

int Context::textHeight(int size) {
    // Approximate using a reference character
    const auto& e = textCache.get("Ag", size);
    return e.height;
}

int Context::drawTextWrapped(const char* text, int x, int y, int size,
                              int maxW, SDL_Color color, bool doDraw) {
    const int lineH = textHeight(size) + 1;
    int curY = y;

    char line[256]; line[0] = '\0';
    char word[128];
    const char* p = text;

    auto flush = [&]() {
        if (line[0] == '\0') return;
        if (doDraw) drawText(line, x, curY, size, color);
        curY += lineH;
        line[0] = '\0';
    };

    while (*p) {
        // Skip leading spaces
        while (*p == ' ') ++p;
        if (!*p) break;

        // Collect next word
        int wlen = 0;
        while (*p && *p != ' ' && wlen < 126) word[wlen++] = *p++;
        word[wlen] = '\0';

        // Build candidate line
        char candidate[256];
        if (line[0]) {
            snprintf(candidate, sizeof(candidate), "%s %s", line, word);
        } else {
            snprintf(candidate, sizeof(candidate), "%s", word);
        }

        if (line[0] && textWidth(candidate, size) > maxW) {
            flush();
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
    }
    flush();
    return curY - y;
}

void Context::drawPanel(int x, int y, int w, int h, SDL_Color bg, SDL_Color border) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_Rect panel = {x, y, w, h};
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &panel);
}

void Context::drawDarkOverlay(uint8_t alpha, uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    SDL_RenderFillRect(renderer, &full);
}

void Context::drawSeparator(int cx, int y, int halfWidth, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect sep = {cx - halfWidth, y, halfWidth * 2, 1};
    SDL_RenderFillRect(renderer, &sep);
}

// ─── Interactive Elements ───────────────────────────────────────────────────

static float smoothstep(float t) {
    t = std::max(0.f, std::min(1.f, t));
    return t * t * (3.f - 2.f * t);
}

bool Context::menuItem(int idx, const char* label, int cx, int y, int w, int h,
                       SDL_Color accent, bool sel, int fontSize, int selFontSize) {
    // Bounds for hit testing
    int rx = cx - w / 2;
    int ry = y - 4;
    int rw = w;
    int rh = h;

    // Mouse hover detection
    bool hovered = pointInRect(mouseX, mouseY, rx, ry, rw, rh);
    if (hovered) hoveredItem = idx;

    bool activated = false;
    if (hovered && mouseClicked) {
        clickedItem = idx;
        activated = true;
        mouseClicked = false;
        clickCooldownFrames = 3;
    }

    // Animate focus (smooth interpolation)
    bool focused = sel || hovered;
    float& anim = (idx >= 0 && idx < MAX_ANIM_ITEMS) ? itemAnim[idx] : itemAnim[0];
    float target = focused ? 1.0f : 0.0f;
    float speed = 12.0f; // animation speed
    anim += (target - anim) * std::min(1.0f, speed * dt);
    if (fabsf(anim - target) < 0.01f) anim = target;

    float a = smoothstep(anim);

    // Background highlight
    if (a > 0.01f) {
        Uint8 bgAlpha = (Uint8)(a * 30);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, bgAlpha);
        SDL_Rect bg = {rx, ry, rw, rh};
        SDL_RenderFillRect(renderer, &bg);

        // Left accent bar (slides in)
        int barW = (int)(3.0f * a);
        if (barW > 0) {
            Uint8 barAlpha = (Uint8)(180 * a);
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, barAlpha);
            SDL_Rect bar = {rx, ry, barW, rh};
            SDL_RenderFillRect(renderer, &bar);
        }
    }

    // Text color: interpolate between gray and accent
    SDL_Color c;
    c.r = (Uint8)(Color::Gray.r + (accent.r - Color::Gray.r) * a);
    c.g = (Uint8)(Color::Gray.g + (accent.g - Color::Gray.g) * a);
    c.b = (Uint8)(Color::Gray.b + (accent.b - Color::Gray.b) * a);
    c.a = 255;

    // Font size: interpolate
    int fs = fontSize + (int)((selFontSize - fontSize) * a);

    // Slight indent when focused
    int indent = (int)(8.0f * a);

    // Draw label
    const auto& entry = textCache.get(label, fs);
    if (entry.texture) {
        SDL_SetTextureColorMod(entry.texture, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(entry.texture, c.a);
        SDL_Rect dst = {cx - entry.width / 2 + indent, y + (h - entry.height) / 2,
                        entry.width, entry.height};
        SDL_RenderCopy(renderer, entry.texture, nullptr, &dst);
    }

    return activated;
}

int Context::sliderRow(int idx, const char* label, const char* value,
                       int cx, int y, int w, int h,
                       SDL_Color accent, bool sel, bool leftKey, bool rightKey) {
    int rx = cx - w / 2;
    int ry = y - 4;
    int rw = w;
    int rh = h;

    // Mouse hover
    bool hovered = pointInRect(mouseX, mouseY, rx, ry, rw, rh);
    if (hovered) hoveredItem = idx;

    // Animate
    bool focused = sel || hovered;
    float& anim = (idx >= 0 && idx < MAX_ANIM_ITEMS) ? itemAnim[idx] : itemAnim[0];
    float target = focused ? 1.0f : 0.0f;
    anim += (target - anim) * std::min(1.0f, 12.0f * dt);
    if (fabsf(anim - target) < 0.01f) anim = target;
    float a = smoothstep(anim);

    // Background
    if (a > 0.01f) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, (Uint8)(a * 25));
        SDL_Rect bg = {rx, ry, rw, rh};
        SDL_RenderFillRect(renderer, &bg);

        int barW = (int)(3.0f * a);
        if (barW > 0) {
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, (Uint8)(180 * a));
            SDL_Rect bar = {rx, ry, barW, rh};
            SDL_RenderFillRect(renderer, &bar);
        }
    }

    // Color
    SDL_Color c;
    c.r = (Uint8)(Color::Gray.r + (accent.r - Color::Gray.r) * a);
    c.g = (Uint8)(Color::Gray.g + (accent.g - Color::Gray.g) * a);
    c.b = (Uint8)(Color::Gray.b + (accent.b - Color::Gray.b) * a);
    c.a = 255;

    int fs = 20 + (int)(2 * a);

    // Build display text
    char display[128];
    if (focused) {
        snprintf(display, sizeof(display), "<  %s  %s  >", label, value);
    } else {
        snprintf(display, sizeof(display), "%s  %s", label, value);
    }

    int indent = (int)(6.0f * a);
    drawTextCentered(display, y + (h - textHeight(fs)) / 2 + indent / 3, fs, c);

    // Click on left/right arrows
    int delta = 0;
    if (leftKey)  delta = -1;
    if (rightKey) delta = +1;

    if (hovered && mouseClicked) {
        if (mouseX < cx) delta = -1;
        else delta = +1;
        mouseClicked = false;
        clickCooldownFrames = 3;
    }

    return delta;
}

void Context::drawHintBar(const HintPair* pairs, int count, int y) {
    std::string text = buildHintBar(pairs, count, usingGamepad);
    drawTextCentered(text.c_str(), y, 13, Color::HintGray);
}

bool Context::pointInRect(int px, int py, int rx, int ry, int rw, int rh) const {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Win98 Drawing Primitives
// ═════════════════════════════════════════════════════════════════════════════

void Context::drawDesktop() {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    if (desktopBg) {
        SDL_SetTextureBlendMode(desktopBg, SDL_BLENDMODE_NONE);
        SDL_SetTextureColorMod(desktopBg, 255, 255, 255);
        SDL_SetTextureAlphaMod(desktopBg, 255);
        SDL_Rect dst = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(renderer, desktopBg, nullptr, &dst);
    } else {
#ifdef __ANDROID__
        // On Android show a dark background instead of the Win98 teal
        SDL_SetRenderDrawColor(renderer, 18, 20, 30, 255);
#else
        SDL_SetRenderDrawColor(renderer, W98::Desktop.r, W98::Desktop.g, W98::Desktop.b, 255);
#endif
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderFillRect(renderer, &full);
    }
}

void Context::drawWin98Bevel(int x, int y, int w, int h, bool raised) {
    if (w < 2 || h < 2) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    auto ln = [&](SDL_Color c, int x1, int y1, int x2, int y2) {
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
    };

    SDL_Color outerTL = raised ? W98::White      : W98::DarkShadow;
    SDL_Color innerTL = raised ? W98::Light      : W98::Shadow;
    SDL_Color innerBR = raised ? W98::Shadow     : W98::Light;
    SDL_Color outerBR = raised ? W98::DarkShadow : W98::White;

    ln(outerTL, x,     y,     x+w-2, y    );
    ln(outerTL, x,     y,     x,     y+h-2);
    ln(outerBR, x,     y+h-1, x+w-1, y+h-1);
    ln(outerBR, x+w-1, y,     x+w-1, y+h-1);

    if (w >= 4 && h >= 4) {
        ln(innerTL, x+1, y+1,   x+w-3, y+1  );
        ln(innerTL, x+1, y+1,   x+1,   y+h-3);
        ln(innerBR, x+1, y+h-2, x+w-2, y+h-2);
        ln(innerBR, x+w-2, y+1, x+w-2, y+h-2);
    }
}

void Context::drawWin98Window(int x, int y, int w, int h, const char* title, bool active) {
    const int tH = W98::TitleH;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, W98::Silver.r, W98::Silver.g, W98::Silver.b, 255);
    SDL_Rect body = {x, y, w, h};
    SDL_RenderFillRect(renderer, &body);

    drawWin98Bevel(x, y, w, h, true);

    // Title bar
    SDL_Color barColor = active ? W98::Navy : W98::Shadow;
    SDL_SetRenderDrawColor(renderer, barColor.r, barColor.g, barColor.b, 255);
    SDL_Rect titleBar = {x+3, y+3, w-6, tH};
    SDL_RenderFillRect(renderer, &titleBar);

    if (title && title[0]) {
        const auto& te = textCache.get(title, 14);
        if (te.texture) {
            SDL_SetTextureColorMod(te.texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(te.texture, 255);
            SDL_Rect dst = {x+7, y+3+(tH-te.height)/2, te.width, te.height};
            SDL_RenderCopy(renderer, te.texture, nullptr, &dst);
        }
    }

    // Close button
    int cbSz = tH - 4;
    int cbX  = x + w - 3 - cbSz;
    int cbY  = y + 5;
    SDL_SetRenderDrawColor(renderer, W98::Silver.r, W98::Silver.g, W98::Silver.b, 255);
    SDL_Rect cb = {cbX, cbY, cbSz, cbSz};
    SDL_RenderFillRect(renderer, &cb);
    drawWin98Bevel(cbX, cbY, cbSz, cbSz, true);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    int m = std::max(2, cbSz/4);
    SDL_RenderDrawLine(renderer, cbX+m,      cbY+m,      cbX+cbSz-m-1, cbY+cbSz-m-1);
    SDL_RenderDrawLine(renderer, cbX+m+1,    cbY+m,      cbX+cbSz-m,   cbY+cbSz-m-1);
    SDL_RenderDrawLine(renderer, cbX+cbSz-m-1, cbY+m,   cbX+m,         cbY+cbSz-m-1);
    SDL_RenderDrawLine(renderer, cbX+cbSz-m,   cbY+m,   cbX+m+1,       cbY+cbSz-m-1);

    // Separator below title bar
    SDL_SetRenderDrawColor(renderer, W98::Shadow.r, W98::Shadow.g, W98::Shadow.b, 255);
    SDL_RenderDrawLine(renderer, x+3, y+3+tH, x+w-4, y+3+tH);
    SDL_SetRenderDrawColor(renderer, W98::White.r, W98::White.g, W98::White.b, 255);
    SDL_RenderDrawLine(renderer, x+3, y+4+tH, x+w-4, y+4+tH);
}

bool Context::win98Button(int idx, const char* label, int x, int y, int w, int h, bool sel) {
    bool hovered  = pointInRect(mouseX, mouseY, x, y, w, h);
    if (hovered) hoveredItem = idx;

    bool activated = false;
    if (hovered && mouseClicked) { clickedItem = idx; activated = true; buttonFired = true; mouseClicked = false; clickCooldownFrames = 3; }

    bool pressed = hovered && mouseDown;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, W98::Silver.r, W98::Silver.g, W98::Silver.b, 255);
    SDL_Rect face = {x, y, w, h};
    SDL_RenderFillRect(renderer, &face);

    // Keyboard-focus dotted inner rect
    if (sel && !pressed) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_Rect inner = {x+3, y+3, w-6, h-6};
        SDL_RenderDrawRect(renderer, &inner);
    }

    drawWin98Bevel(x, y, w, h, !pressed);

    const auto& te = textCache.get(label, 14);
    if (te.texture) {
        SDL_SetTextureColorMod(te.texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(te.texture, 255);
        int ox = pressed ? 1 : 0;
        int tx = x + (w - te.width) / 2 + ox;
        int ty = y + (h - te.height) / 2 + ox;
        // Clip text horizontally to button interior so wide labels never overflow
        int srcX = 0, drawW = te.width;
        if (tx < x + 2)              { srcX = x + 2 - tx; drawW -= srcX; tx = x + 2; }
        if (tx + drawW > x + w - 2)    drawW = x + w - 2 - tx;
        if (drawW > 0) {
            SDL_Rect src = {srcX, 0, drawW, te.height};
            SDL_Rect dst = {tx, ty, drawW, te.height};
            SDL_RenderCopy(renderer, te.texture, &src, &dst);
        }
    }

    return activated;
}

void Context::drawWin98TextField(int x, int y, int w, int h, const char* text,
                                  bool focused, bool password, float blinkT) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect field = {x, y, w, h};
    SDL_RenderFillRect(renderer, &field);
    drawWin98Bevel(x, y, w, h, false);

    std::string display;
    if (text) {
        display = password ? std::string(strlen(text), '*') : std::string(text);
    }
    if (focused && (int)(blinkT * 2) % 2 == 0) display += '|';

    if (!display.empty()) {
        const auto& te = textCache.get(display.c_str(), 14);
        if (te.texture) {
            SDL_SetTextureColorMod(te.texture, 0, 0, 0);
            SDL_SetTextureAlphaMod(te.texture, 255);
            SDL_Rect dst = {x+5, y+(h-te.height)/2,
                            std::min(te.width, w-8), te.height};
            SDL_RenderCopy(renderer, te.texture, nullptr, &dst);
        }
    }
}

void Context::drawWin98StatusBar(int y, const char* text) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, W98::Silver.r, W98::Silver.g, W98::Silver.b, 255);
    SDL_Rect bar = {0, y, SCREEN_W, SCREEN_H - y};
    SDL_RenderFillRect(renderer, &bar);
    SDL_SetRenderDrawColor(renderer, W98::Shadow.r, W98::Shadow.g, W98::Shadow.b, 255);
    SDL_RenderDrawLine(renderer, 0, y, SCREEN_W, y);
    SDL_SetRenderDrawColor(renderer, W98::White.r, W98::White.g, W98::White.b, 255);
    SDL_RenderDrawLine(renderer, 0, y+1, SCREEN_W, y+1);
    if (text && text[0]) {
        const auto& te = textCache.get(text, 12);
        if (te.texture) {
            SDL_SetTextureColorMod(te.texture, 0, 0, 0);
            SDL_SetTextureAlphaMod(te.texture, 255);
            SDL_Rect dst = {6, y+(SCREEN_H-y-te.height)/2, te.width, te.height};
            SDL_RenderCopy(renderer, te.texture, nullptr, &dst);
        }
    }
}

} // namespace UI
