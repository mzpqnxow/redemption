/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Product name: redemption, a FLOSS RDP proxy
   Copyright (C) Wallix 2010
   Author(s): Christophe Grosjean

   New RDP Orders Coder / Decoder : Common parts and constants

*/

#if !defined(__CORE_RDP_ORDERS_RDPORDERSCOMMON_HPP__)
#define __CORE_RDP_ORDERS_RDPORDERSCOMMON_HPP__

#include "constants.hpp"
#include "stream.hpp"
#include "rect.hpp"
#include "bitfu.hpp"
#include "bitmap.hpp"
#include "client_info.hpp"
#include "RDP/rdp.hpp"

//MS-RDPEGDI 3.3.5.1.1.1       Construction of a Primary Drawing Order
//====================================================================

//  All primary drawing orders MUST conform to the structure and rules defined
//  in section 2.2.2.2.1.1.2.

// To efficiently construct a primary drawing order, the server MUST use a
// Primary Drawing Order History (section 3.2.1.1) store. This store holds three
// pieces of information:

//  - Last primary order type constructed.

//  - Current bounding rectangle.

//  - Per-order record of the last value used in each field.

//  These stored records allow the server to use the minimum amount of data when
//  constructing an order; if a field is unchanged from the value that it had
//  when the order type was last sent, it SHOULD NOT be included in the order
//  being constructed. Hence, only fields that have new values are required to
//  be sent to the client. The fields that are present in the order MUST be
//  indicated by the fieldFlags field.

//  If all of the Coord-type fields (see section 2.2.2.2.1.1.1.1) in an order
//  can be represented as a signed delta in the range -127 to 128 from the
//  previous field value, the size of the order SHOULD be optimized by using
//  delta-coordinates (see sections 2.2.2.2.1.1.1.1 and 2.2.2.2.1.1.2). In that
//  case, all of the fields SHOULD be represented using delta-coordinates, and
//  the TS_DELTA_COORDINATES (0x10) flag MUST be used in the primary drawing
//  order header to indicate this fact.

//  Before a given order is sent, the server MUST also ensure that all of the
//  data required to process the order is accessible to the client. For example,
//  if the order refers to a cached item, that item MUST be present in the
//  client-side cache when the order is processed. Or, if palettized color is
//  being used, the correct palette MUST be applied at the client-side.

//  Once a primary drawing order has been constructed and transmitted to the
//  client, the server MUST update the records in the Primary Drawing Order
//  History (section 3.3.1.2) to ensure that future encodings use the minimum
//  fields and data required.

// MS-RDPEGDI : 2.2.2.2.1.2.1.2 Two-Byte Unsigned Encoding
// =======================================================
// (TWO_BYTE_UNSIGNED_ENCODING)

// The TWO_BYTE_UNSIGNED_ENCODING structure is used to encode a value in
// the range 0x0000 to 0x7FFF by using a variable number of bytes.
// For example, 0x1A1B is encoded as { 0x9A, 0x1B }.
// The most significant bit of the first byte encodes the number of bytes
// in the structure.

// c (1 bit): A 1-bit, unsigned integer field that contains an encoded
// representation of the number of bytes in this structure. 0 implies val2 field
// is not present, if 1 val2 is present.

// val1 (7 bits): A 7-bit, unsigned integer field containing the most
// significant 7 bits of the value represented by this structure.

// val2 (1 byte): An 8-bit, unsigned integer containing the least significant
// bits of the value represented by this structure.

// MS-RDPEGDI : 2.2.2.2.1.2.1.3 Two-Byte Signed Encoding
// =====================================================
// (TWO_BYTE_SIGNED_ENCODING)

// The TWO_BYTE_SIGNED_ENCODING structure is used to encode a value in
// the range -0x3FFF to 0x3FFF by using a variable number of bytes. For example,
// -0x1A1B is encoded as { 0xDA, 0x1B }, and -0x0002 is encoded as { 0x42 }.
// The most significant bits of the first byte encode the number of bytes in
// the structure and the sign.

// c (1 bit): A 1-bit, unsigned integer field containing an encoded
// representation of the number of bytes in this structure. 0 implies that val2
// is not present, 1 implies it is present.

// s (1 bit): A 1-bit, unsigned integer field containing an encoded
// representation of whether the value is positive or negative. 0 implies
// the value is positive, 1 implies that the value is negative

// val1 (6 bits): A 6-bit, unsigned integer field containing the most
// significant 6 bits of the value represented by this structure.

// val2 (1 byte): An 8-bit, unsigned integer containing the least
// significant bits of the value represented by this structure.

// MS-RDPEGDI : 2.2.2.2.1.2.1.4 Four-Byte Unsigned Encoding
// ========================================================
// (FOUR_BYTE_UNSIGNED_ENCODING)

// The FOUR_BYTE_UNSIGNED_ENCODING structure is used to encode a value in the
// range 0x00000000 to 0x3FFFFFFF by using a variable number of bytes.
// For example, 0x001A1B1C is encoded as { 0x9A, 0x1B, 0x1C }. The two most
// significant bits of the first byte encode the number of bytes in the
// structure.

// c (2 bits): A 2-bit, unsigned integer field containing an encoded
// representation of the number of bytes in this structure.
// 0 : val1 only (1 Byte), 1 : val1 and val2 (2 Bytes),
// 2: val1, val2 and val3 (3 Bytes), 3: val1, val2, val3, val4 (4 Bytes)

// val1 (6 bits): A 6-bit, unsigned integer field containing the most
// significant 6 bits of the value represented by this structure.

// val2 (1 byte): An 8-bit, unsigned integer containing the second most
// significant bits of the value represented by this structure.

// val3 (1 byte): An 8-bit, unsigned integer containing the third most
// significant bits of the value represented by this structure.

// val4 (1 byte): An 8-bit, unsigned integer containing the least
// significant bits of the value represented by this structure.

struct RDPPen {
    uint8_t style;
    uint8_t width;
    uint32_t color;
    RDPPen(uint8_t style, uint8_t width, uint32_t color)
        : style(style), width(width), color(color) {}

    RDPPen() : style(0), width(0), color(0) {
    }

    bool operator==(const RDPPen &other) const {
        return  (this->style == other.style)
             && (this->width == other.width)
             && (this->color == other.color)
             ;
    }
};

struct RDPBrush {
    int8_t org_x;
    int8_t org_y;
    uint8_t style;
    uint8_t hatch;
    uint8_t extra[7];

    RDPBrush() :
        org_x(0),
        org_y(0),
        style(0),
        hatch(0)
        {
            memset(this->extra, 0, 7);
        }

    RDPBrush(int8_t org_x, int8_t org_y, uint8_t style, uint8_t hatch,
             const uint8_t * extra = (const uint8_t*)"\0\0\0\0\0\0\0") :
        org_x(org_x),
        org_y(org_y),
        style(style),
        hatch(hatch)
        {
            memcpy(this->extra, extra, 7);
        }

    bool operator==(const RDPBrush &other) const {
        return  (this->org_x == other.org_x)
             && (this->org_y == other.org_y)
             && (this->style == other.style)
             && (this->hatch == other.hatch)
             && ((this->style != 3) || (0 == memcmp(this->extra, other.extra, 7)))
             ;
    }

};


namespace RDP {

    // control byte
    // ------------
    enum {
        STANDARD   = 0x01, // type of order bit 1
        SECONDARY  = 0x02, // type of order bit 2
        BOUNDS     = 0x04, // the current drawing order is clipped
        CHANGE     = 0x08, // new order (order byte is there)
        DELTA      = 0x10, // coordinate fields are 1 byte delta
        LASTBOUNDS = 0x20, // use previous bounds (no bounds sent)
        SMALL      = 0x40, // -1 on number of bytes for fields
        TINY       = 0x80, // -2 on number of bytes for fields
    };

    enum {
        DESTBLT    = 0,
        PATBLT     = 1,
        SCREENBLT  = 2,
        LINE       = 9,
        RECT       = 10,
        DESKSAVE   = 11,
        MEMBLT     = 13,
        TRIBLT     = 14,
        POLYLINE   = 22,
        GLYPHINDEX = 27,
    };

    enum SecondaryOrderType {
     // TS_CACHE_BITMAP_UNCOMPRESSED - Cache Bitmap - Revision 1
     // (MS-RDPEGDI section 2.2.2.2.1.2.2)
        TS_CACHE_BITMAP_UNCOMPRESSED  = 0,
     // TS_CACHE_COLOR_TABLE - Cache Color Table
     // (MS-RDPEGDI section 2.2.2.2.1.2.4)
        TS_CACHE_COLOR_TABLE      = 1,
     // TS_CACHE_BITMAP_COMPRESSED - Cache Bitmap - Revision 1
     // (MS-RDPEGDI section 2.2.2.2.1.2.2)
        TS_CACHE_BITMAP_COMPRESSED      = 2,
     // TS_CACHE_GLYPH : Cache Glyph - Revision 1
     // (MS-RDPEGDI section 2.2.2.2.1.2.5)
     // or Cache Glyph - Revision 2
     // (MS-RDPEGDI section 2.2.2.2.1.2.6) (choice through extra flags)
        TS_CACHE_GLYPH     = 3,
     // TS_CACHE_BITMAP_UNCOMPRESSED_REV2 : Cache Bitmap - Revision 2
     // (MS-RDPEGDI section 2.2.2.2.1.2.3)
        TS_CACHE_BITMAP_UNCOMPRESSED_REV2 = 4,
     // TS_CACHE_BITMAP_COMPRESSED_REV2 : Cache Bitmap - Revision 2
     // (MS-RDPEGDI section 2.2.2.2.1.2.3)
        TS_CACHE_BITMAP_COMPRESSED_REV2    = 5,
     // TS_CACHE_BRUSH : Cache Brush
     // (MS-RDPEGDI section 2.2.2.2.1.2.7)
        TS_CACHE_BRUSH    = 7,
     // TS_CACHE_BITMAP_COMPRESSED_REV3 : Cache Bitmap - Revision 3
     // (MS-RDPEGDI section 2.2.2.2.1.2.8)
        TS_CACHE_BITMAP_COMPRESSED_REV3     = 8
    };

    enum e_bounds {
        LEFT = 0,
        TOP = 1,
        RIGHT = 2,
        BOTTOM = 3
    };

} /* namespace */



inline static bool is_1_byte(int16_t value){
    return (value >= -128) && (value <= 127);
}

inline static uint8_t pounder_bound(int16_t delta, uint8_t pound)
{
    return ((pound * (delta != 0)) << (4 * is_1_byte(delta)));
}



class Bounds {

    // bounds byte (which clipping is sent if any)
    // -------------------------------------------
    // order of clipping field always is top, left, right, bottom
    // 0x01: top    bound (absolute 2 bytes)
    // 0x02: left   bound (absolute 2 bytes)
    // 0x04: right  bound (absolute 2 bytes)
    // 0x08: bottom bound (absolute 2 bytes)
    // 0x10: top    bound (relative 1 byte)
    // 0x20: left   bound (relative 1 byte)
    // 0x40: right  bound (relative 1 byte)
    // 0x80: bottom bound (relative 1 byte)

    public:

    uint8_t bounds_flags;
    int16_t absolute_bounds[4];
    int16_t delta_bounds[4];

    Bounds(const Rect & oldclip, const Rect & newclip)
    {
        int16_t old_bounds[4];

        using namespace RDP;
        old_bounds[LEFT]   = oldclip.x;
        old_bounds[TOP]    = oldclip.y;
        old_bounds[RIGHT]  = oldclip.x + oldclip.cx - 1;
        old_bounds[BOTTOM] = oldclip.y + oldclip.cy - 1;

        this->absolute_bounds[LEFT]   = newclip.x;
        this->absolute_bounds[TOP]    = newclip.y;
        this->absolute_bounds[RIGHT]  = newclip.x + newclip.cx - 1;
        this->absolute_bounds[BOTTOM] = newclip.y + newclip.cy - 1;

        this->delta_bounds[LEFT]   = this->absolute_bounds[LEFT]   - old_bounds[LEFT];
        this->delta_bounds[TOP]    = this->absolute_bounds[TOP]    - old_bounds[TOP];
        this->delta_bounds[RIGHT]  = this->absolute_bounds[RIGHT]  - old_bounds[RIGHT];
        this->delta_bounds[BOTTOM] = this->absolute_bounds[BOTTOM] - old_bounds[BOTTOM];

        this->bounds_flags =
            pounder_bound(this->delta_bounds[LEFT],  (1<<LEFT))
          | pounder_bound(this->delta_bounds[TOP],   (1<<TOP))
          | pounder_bound(this->delta_bounds[RIGHT], (1<<RIGHT))
          | pounder_bound(this->delta_bounds[BOTTOM],(1<<BOTTOM))
          ;
    }

    void emit(Stream & stream)
    {
        using namespace RDP;

        if (this->bounds_flags != 0){
            stream.out_uint8(this->bounds_flags);
            for (unsigned b = LEFT ; b <= BOTTOM ; ++b){
                if (this->bounds_flags & (1<<b)) {
                    stream.out_uint16_le(this->absolute_bounds[b]);
                }
                else if (this->bounds_flags & (0x10<<b)) {
                    stream.out_uint8(this->delta_bounds[b]);
                }
            }
        }
    }
};


class RDPPrimaryOrderHeader
{
    public:
    uint8_t control;
    uint32_t fields;

    RDPPrimaryOrderHeader(uint8_t control, uint32_t fields)
        : control(control), fields(fields) {}

    void emit_coord(Stream & stream, uint32_t base, int16_t coord, int16_t oldcoord) const {
        using namespace RDP;

        if (this->control & DELTA){
            if (this->fields & base){
                stream.out_uint8(coord-oldcoord);
            }
        }
        else {
            if (this->fields & base){
                stream.out_uint16_le(coord);
            }
        }
    }

    void receive_coord(Stream & stream, uint32_t base, int16_t & coord) const
    {
        using namespace RDP;

        if (this->control & DELTA){
            if (this->fields & base) {
                coord = coord + stream.in_sint8();
            }
        }
        else {
            if (this->fields & base) {
                coord = stream.in_sint16_le();
            }
        }
    }


    void emit_rect(Stream & stream, uint32_t base, const Rect & rect, const Rect & oldr) const {
        using namespace RDP;

        if (this->control & DELTA){
            if (this->fields & base){
                stream.out_uint8(rect.x-oldr.x);
            }
            if (this->fields & (base << 1)){
                stream.out_uint8(rect.y-oldr.y);
            }
            if (this->fields & (base << 2)){
                stream.out_uint8(rect.cx-oldr.cx);
            }
            if (this->fields & (base << 3)){
                stream.out_uint8(rect.cy-oldr.cy);
            }
        }
        else {
            if (this->fields & base){
                stream.out_uint16_le(rect.x);
            }
            if (this->fields & (base << 1)){
                stream.out_uint16_le(rect.y);
            }
            if (this->fields & (base << 2)){
                stream.out_uint16_le(rect.cx);
            }
            if (this->fields & (base << 3)){
                stream.out_uint16_le(rect.cy);
            }
        }
    }

    void receive_rect(Stream & stream, uint32_t base, Rect & rect) const
    {
        using namespace RDP;

        if (this->control & DELTA){
            if (this->fields & base) {
                rect.x = rect.x + stream.in_sint8();
            }
            if (this->fields & (base << 1)) {
                rect.y = rect.y + stream.in_sint8();
            }
            if (this->fields & (base << 2)) {
                rect.cx = rect.cx + stream.in_sint8();
            }
            if (this->fields & (base << 3)) {
                rect.cy = rect.cy + stream.in_sint8();
            }
        }
        else {
            if (this->fields & base) {
                rect.x = stream.in_sint16_le();
            }
            if (this->fields & (base << 1)) {
                rect.y = stream.in_sint16_le();
            }
            if (this->fields & (base << 2)) {
                rect.cx = stream.in_sint16_le();
            }
            if (this->fields & (base << 3)) {
                rect.cy = stream.in_sint16_le();
            }
        }
    }

    void emit_src(Stream & stream, uint32_t base,
                  uint16_t srcx, uint16_t srcy,
                  uint16_t oldx, uint16_t oldy) const {

        using namespace RDP;

        if (this->control & DELTA){
            if (this->fields & base){
                stream.out_uint8(srcx-oldx);
            }
            if (this->fields & (base << 1)){
                stream.out_uint8(srcy-oldy);
            }
        }
        else {
            if (this->fields & base){
                stream.out_uint16_le(srcx);
            }
            if (this->fields & (base << 1)){
                stream.out_uint16_le(srcy);
            }
        }
    }

    void receive_src(Stream & stream, uint32_t base,
                     uint16_t & srcx, uint16_t & srcy) const
    {
        using namespace RDP;

        if (this->control & DELTA){
            if (this->fields & base) {
                srcx = srcx + stream.in_sint8();
            }
            if (this->fields & (base << 1)) {
                srcy = srcy + stream.in_sint8();
            }
        }
        else {
            if (this->fields & base) {
                srcx = stream.in_sint16_le();
            }
            if (this->fields & (base << 1)) {
                srcy = stream.in_sint16_le();
            }
        }
    }

    void emit_pen(Stream & stream, uint32_t base,
                  const RDPPen & pen,
                  const RDPPen & old_pen) const {

        using namespace RDP;
        if (this->fields & base) {
            stream.out_uint8(pen.style);
        }
        if (this->fields & (base << 1)) {
            stream.out_sint8(pen.width);
         }
        if (this->fields & (base << 2)) {
            stream.out_uint8(pen.color);
            stream.out_uint8(pen.color >> 8);
            stream.out_uint8(pen.color >> 16);
        }
    }

    void receive_pen(Stream & stream, uint32_t base, RDPPen & pen) const
    {
        using namespace RDP;

        if (this->fields & base) {
            pen.style = stream.in_uint8();
        }

        if (this->fields & (base << 1)) {
            pen.width = stream.in_uint8();
        }

        if (this->fields & (base << 2)) {
            uint8_t r = stream.in_uint8();
            uint8_t g = stream.in_uint8();
            uint8_t b = stream.in_uint8();
            pen.color = r + (g << 8) + (b << 16);
        }
    }

    void emit_brush(Stream & stream, uint32_t base,
                  const RDPBrush & brush,
                  const RDPBrush & old_brush) const {

        using namespace RDP;
        if (this->fields & base) {
            stream.out_sint8(brush.org_x);
        }
        if (this->fields & (base << 1)) {
            stream.out_sint8(brush.org_y);
         }
        if (this->fields & (base << 2)) {
            stream.out_uint8(brush.style);
        }
        if (this->fields & (base << 3)) {
            stream.out_uint8(brush.hatch);
        }

        if (this->fields & (base << 4)){
            stream.out_uint8(brush.extra[0]);
            stream.out_uint8(brush.extra[1]);
            stream.out_uint8(brush.extra[2]);
            stream.out_uint8(brush.extra[3]);
            stream.out_uint8(brush.extra[4]);
            stream.out_uint8(brush.extra[5]);
            stream.out_uint8(brush.extra[6]);
        }
    }

    void receive_brush(Stream & stream, uint32_t base, RDPBrush & brush) const
    {
        using namespace RDP;

        if (this->fields & base) {
            brush.org_x = stream.in_sint8();
        }
        if (this->fields & (base << 1)) {
            brush.org_y = stream.in_sint8();
        }
        if (this->fields & (base << 2)) {
            brush.style = stream.in_uint8();
        }
        if (this->fields & (base << 3)) {
            brush.hatch = stream.in_uint8();
        }
        if (this->fields & (base << 4)){
            brush.extra[0] = stream.in_uint8();
            brush.extra[1] = stream.in_uint8();
            brush.extra[2] = stream.in_uint8();
            brush.extra[3] = stream.in_uint8();
            brush.extra[4] = stream.in_uint8();
            brush.extra[5] = stream.in_uint8();
            brush.extra[6] = stream.in_uint8();
        }
    }
};

// Common part for Primary Drawing Orders
// ---------------------------------------

// common part of Primary Drawing Orders (last_order, bounding rectangle)

class RDPOrderCommon {
    public:

    // for primary orders : kept in state
    uint8_t order;
    Rect clip;

    RDPOrderCommon(int order, Rect clip) :
        order(order), clip(clip)
    {
    }

    bool operator==(const RDPOrderCommon &other) const {
        return  (this->order == other.order)
             && (this->clip == other.clip);
    }

private:
    void _emit(Stream & stream, RDPPrimaryOrderHeader & header)
    {
        using namespace RDP;

        int size = 1;
        switch (this->order)
        {
            case TRIBLT:
            case GLYPHINDEX:
                size = 3;
                break;

            case PATBLT:
            case MEMBLT:
            case LINE:
                //case POLYGON2:
                //case ELLIPSE2:
                size = 2;
                break;
            case RECT:
            case SCREENBLT:
            case DESTBLT:
            default:
                size = 1;
        }

        int realsize = (header.fields == 0)      ?  0  :
        (header.fields < 0x100)   ?  1  :
        (header.fields < 0x10000) ?  2  :
        3;

        switch (size - realsize){
            case 3:
                header.control |= TINY | SMALL;
                break;
            case 2:
                header.control |= TINY;
                break;
            case 1:
                header.control |= SMALL;
                break;
            default:;
        }

        // know control is known
        stream.out_uint8(header.control);

        if (header.control & CHANGE){
            stream.out_uint8(order);
        }

        if (header.fields){
            stream.out_uint8(header.fields & 0xFF);
            if (header.fields >= 0x100){
                stream.out_uint8((header.fields >> 8) & 0xFF);
                if (header.fields >= 0x10000){
                    stream.out_uint8((header.fields >> 16) & 0xFF);
                }
            }
        }
    }

public:
    void emit(Stream & stream, RDPPrimaryOrderHeader & header, const RDPOrderCommon & oldcommon)
    {

        using namespace RDP;

        Bounds bounds(oldcommon.clip, this->clip);

        header.control |= (this->order != oldcommon.order) * CHANGE;

        if (header.control & BOUNDS){
            header.control |= ((bounds.bounds_flags == 0) * LASTBOUNDS);
        }

        this->_emit(stream, header);

        if (header.control & BOUNDS){
            if (!(header.control & LASTBOUNDS)){
                bounds.emit(stream);
            }
        }
        else {
            this->clip = oldcommon.clip;
        }
    }

    const  RDPPrimaryOrderHeader receive(Stream & stream, uint8_t control)
    {

        using namespace RDP;

        RDPPrimaryOrderHeader header(control, 0);

//        LOG(LOG_INFO, "reading control (%p): %.2x %s%s%s%s%s%s%s%s\n", stream.p,
//            control,
//            (control & STANDARD  )?"STD ":"    ",
//            (control & SECONDARY )?"SEC ":"    ",
//            (control & BOUNDS    )?"BOU ":"    ",
//            (control & CHANGE    )?"CHA ":"    ",
//            (control & DELTA     )?"DTA ":"    ",
//            (control & LASTBOUNDS)?"LBO ":"    ",
//            (control & SMALL     )?"SMA ":"    ",
//            (control & TINY      )?"TIN ":"    "
//        );

        if (control & CHANGE) {
            this->order = stream.in_uint8();
        }

        size_t size = 1;
        switch (this->order)
        {
            case TRIBLT:
            case GLYPHINDEX:
                size = 3;
                break;

            case PATBLT:
            case MEMBLT:
            case LINE:
            //case POLYGON2:
            //case ELLIPSE2:
                size = 2;
                break;
            case RECT:
            case SCREENBLT:
            case DESTBLT:
            default:
                size = 1;
        }
        if (header.control & SMALL) {
            size = (size<=1)?0:size-1;
        }
        if (header.control & TINY) {
            size = (size<=2)?0:size-2;
        }

        for (size_t i = 0; i < size; i++) {
            int bits = stream.in_uint8();
            header.fields |= bits << (i * 8);
        }

//            LOG(LOG_INFO, "control=%.2x order=%d  size=%d fields=%.6x assert=%d\n", control, order, size, fields, (0 == (fields & ~0x3FF)));

        switch (this->order){
        case DESTBLT:
            assert(!(header.fields & ~0x1F));
        break;
        case PATBLT:
            assert(!(header.fields & ~0xFFF));
        break;
        case SCREENBLT:
            assert(!(header.fields & ~0x7F));
        break;
        case LINE:
            assert(!(header.fields & ~0x3FF));
        break;
        case RECT:
            assert(!(header.fields & ~0x7F));
        break;
        case DESKSAVE:
        break;
        case MEMBLT:
            assert(!(header.fields & ~0x1FF));
        break;
        case GLYPHINDEX:
            assert(!(header.fields & ~0x3FFFFF));
        break;
        default:
            LOG(LOG_INFO, "Order is Unknown (%u)\n", this->order);
            assert(false);
        }

        if (header.control & BOUNDS) {
            if (!(header.control & LASTBOUNDS)){
                int bound_fields = stream.in_uint8();
                uint16_t bounds[4] = 
                    { static_cast<uint16_t>(this->clip.x)
                    , static_cast<uint16_t>(this->clip.y)
                    , static_cast<uint16_t>(this->clip.x + this->clip.cx - 1) 
                    , static_cast<uint16_t>(this->clip.y + this->clip.cy - 1)
                    };

                if (bound_fields & 1) {
                    bounds[0] = stream.in_sint16_le();
                } else if (bound_fields & 0x10) {
                    bounds[0] += stream.in_sint8();
                }

                if (bound_fields & 2) {
                    bounds[1] = stream.in_sint16_le();
                } else if (bound_fields & 0x20) {
                    bounds[1] += stream.in_sint8();
                }

                if (bound_fields & 4) {
                    bounds[2] = stream.in_sint16_le();
                } else if (bound_fields & 0x40) {
                    bounds[2] += stream.in_sint8();
                }

                if (bound_fields & 8) {
                    bounds[3] = stream.in_sint16_le();
                } else if (bound_fields & 0x80) {
                    bounds[3] += stream.in_sint8();
                }

                this->clip.x = bounds[0];
                this->clip.y = bounds[1];
                this->clip.cx = bounds[2] - bounds[0] +1;
                this->clip.cy = bounds[3] - bounds[1] +1;
            }
        }

        return header;
    }

    size_t str(char * buffer, size_t sz, bool showclip = true) const
    {
        size_t lg = sz;
        if (showclip){
            lg  = snprintf(buffer, sz, "order(%d clip(%d,%d,%d,%d)):",
                this->order,
                this->clip.x, this->clip.y, this->clip.cx, this->clip.cy);
        }
        else {
            lg  = snprintf(buffer, sz, "order(%d):", this->order);
        }
        return (lg < sz)?lg:sz;
    }


};

#endif
