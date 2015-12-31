/*

*/

#define BOOST_AUTO_TEST_MAIN
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE TestRDPQtDrawable
#include <boost/test/auto_unit_test.hpp>

#define LOGNULL
//#define LOGPRINT

#include "RDP/orders/RDPOrdersPrimaryOpaqueRect.hpp"
#include "RDP/orders/RDPOrdersPrimaryLineTo.hpp"
#include "RDPQtDrawable.hpp"
#include <QApplication>


BOOST_AUTO_TEST_CASE(TestRDPQtDrawable)
{
    int argc(0);
    char *argv[] {"myprog"};
    QApplication app(argc, argv);
    RDPQtDrawable drawer(400, 300);
    
    // rect test
    Rect rect(100, 100, 100, 100);
    uint32_t color(0xFF);
    RDPOpaqueRect opaqueRect(rect, color);
    
    //line test
    RDPPen pen;
    RDPLineTo line(0,
                   0, 0, 400, 300,
                   0x0F,
                   0,
                   pen);

    RDPLineTo line2(0,
                   0, 20, 400, 320,
                   0x0F,
                   0,
                   pen);
    
    drawer.draw(line, rect);
    drawer.draw(opaqueRect, rect);
    drawer.draw(line2, rect);
    drawer.flush();
    
    drawer.show();
    app.exec();
    
}

