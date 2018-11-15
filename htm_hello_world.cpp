#include <immintrin.h>
#include <iostream>
using namespace std;

int main(int argc, char ** argv) {
    int status;
    if ((status = _xbegin()) == _XBEGIN_STARTED) {
        _xend();
        cout<<"committed empty hardware tx successfully"<<endl;
    } else {
        cout<<"aborted empty hardware tx"<<endl;
    }
    return 0;
}
