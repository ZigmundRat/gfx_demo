#ifndef HTCW_GFX_BITMAP
#define HTCW_GFX_BITMAP
#include "gfx_core.hpp"
#include "gfx_pixel.hpp"
#include "gfx_positioning.hpp"
namespace gfx {
    namespace helpers {
        template<typename Destination,bool Suspend>
        struct suspend_helper {
            inline static gfx_result suspend(Destination& dst) {
                return gfx_result::success;
            }
            inline static gfx_result resume(Destination& dst) {
                return gfx_result::success;
            }
        };
        template<typename Destination>
        struct suspend_helper<Destination,true> {
            inline static gfx_result suspend(Destination& dst) {
                return dst.suspend();
            }
            inline static gfx_result resume(Destination& dst) {
                return dst.resume();
            }
        };
        template<typename Destination,bool Batch>
        struct batch_helper {
            inline static gfx_result begin_batch(Destination& dst,rect16& rect) {
                return gfx_result::success;
            }
            inline static gfx_result write_batch(Destination& dst, point16 location, typename Destination::pixel_type color) {
                return dst.point(location,color);
            }
            inline static gfx_result commit_batch(Destination& dst) {
                return gfx_result::success;
            }
        };
        template<typename Destination>
        struct batch_helper<Destination,true> {
            inline static gfx_result begin_batch(Destination& dst,rect16& rect) {
                return dst.begin_batch(rect);
            }
            inline static gfx_result write_batch(Destination& dst, point16 location, typename Destination::pixel_type color) {
                return dst.write_batch(color);
            }
            inline static gfx_result commit_batch(Destination& dst) {
                return dst.commit_batch();
            }
        };

        template<typename Source,typename Destination,bool AllowBlt=true>
        struct bmp_copy_to_helper {
            static inline gfx_result copy_to(const Source& src,const rect16& srcr,Destination& dst,const rect16& dstr) {
                size_t dy=0,dye=dstr.height();
                size_t dx,dxe = dstr.width();
                gfx_result r = helpers::suspend_helper<Destination,Destination::caps::suspend>::suspend(dst);
                if(gfx_result::success!=r)
                    return r;
                r = helpers::batch_helper<Destination,Destination::caps::batch>::begin_batch(dst,dstr);
                if(gfx_result::success!=r)
                    return r;
                int sox = srcr.left(),soy=srcr.top();
                int dox = dstr.left(),doy=dstr.top();
                while(dy<dye) {
                    dx=0;
                    
                    while(dx<dxe) {
                        typename Source::pixel_type spx;
                        r=src.point(point16(sox+dx,soy+dy),&spx);
                        if(gfx_result::success!=r)
                            return r;
                        typename Destination::pixel_type dpx;
                        if(Source::pixel_type::template has_channel_names<channel_name::A>::value) {
                            typename Destination::pixel_type bgpx;
                            r=dst.point(point16(dox+dx,doy+dy),&bgpx);
                            if(gfx_result::success!=r) {
                                return r;
                            }
                            if(!convert(spx,&dpx,&bgpx)) {
                                return gfx_result::not_supported;
                            }
                        } else {    
                            if(!convert(spx,&dpx)) {
                                return gfx_result::invalid_format;
                            }
                        }
                        r = helpers::batch_helper<Destination,Destination::caps::batch>::write_batch(dst,point16(dox+dx,doy+dy),dpx);
                        if(gfx_result::success!=r)
                            return r;
                        ++dx;
                    }
                    ++dy;
                }
                r = helpers::batch_helper<Destination,Destination::caps::batch>::commit_batch(dst);
                if(gfx_result::success!=r)
                    return r;
                return helpers::suspend_helper<Destination,Destination::caps::suspend>::resume(dst);
            }
        };
        // blts a portion of this bitmap to the destination at the specified location.
        // out of bounds regions are cropped
        template<typename Source>
        struct bmp_copy_to_helper<Source,Source,true> {
            static gfx_result copy_to(const Source& src,const rect16& srcr,Source& dst,const rect16& dstr) {
                size_t dy=0,dye=dstr.height();
                size_t dxe = dstr.width();
                if(Source::pixel_type::byte_aligned) {
                const size_t line_len = dstr.width()*Source::pixel_type::packed_size;
                while(dy<dye) {
                    uint8_t* psrc = src.begin()+(((srcr.top()+dy)*src.dimensions().width+srcr.left())*Source::pixel_type::packed_size);
                    uint8_t* pdst = dst.begin()+(((dstr.top()+dy)*dst.dimensions().width+dstr.left())*Source::pixel_type::packed_size);
                    memcpy(pdst,psrc,line_len);
                    ++dy;
                }
                } else {
                    // unaligned version
                    const size_t line_len_bits = dstr.width()*Source::pixel_type::bit_depth;
                    const size_t line_block_pels = line_len_bits>(128*8)?(128*8)/Source::pixel_type::bit_depth:dstr.width();
                    const size_t line_block_bits = line_block_pels*Source::pixel_type::bit_depth;
                    const size_t buf_size = (line_block_bits+7)/8;
                    uint8_t buf[buf_size];
                    buf[buf_size-1]=0;
                    while(dy<dye) {
                        size_t dx=0;
                        while(dx<dxe) {
                            const size_t src_offs = ((srcr.top()+dy)*src.dimensions().width+srcr.left()+dx)*Source::pixel_type::bit_depth;
                            const auto dpx = (dstr.top()+dy)*dst.dimensions().width+dstr.left()+dx;
                            const size_t dst_offs =dpx*Source::pixel_type::bit_depth;
                            const size_t src_offs_bits = src_offs % 8;
                            const size_t dst_offs_bits = dst_offs % 8;
                            uint8_t* psrc = src.begin()+(src_offs/8);
                            uint8_t* pdst = dst.begin()+(dst_offs/8);
                            const size_t block_pels = (line_block_pels+dx>dstr.width())?dstr.width()-dx:line_block_pels;
                            if(0==block_pels) 
                                break;
                            const size_t block_bytes = (block_pels*Source::pixel_type::bit_depth+7)/8;
                            const size_t block_bits = block_pels*Source::pixel_type::bit_depth;
                            const int shift = dst_offs_bits-src_offs_bits;
                            // TODO: Make this more efficient:
                            memcpy(buf,psrc,block_bytes+(dpx+block_pels<=dst.size_pixels()));
                            if(0<shift) {
                                bits::shift_right(buf,0,(sizeof(buf))*8,shift);
                            } else if(0>shift) {
                                bits::shift_left(buf,0,(sizeof(buf))*8,-shift);
                            }
                            bits::set_bits(dst_offs_bits,block_bits, pdst,buf);
                            dx+=block_pels;    
                        }
                        ++dy;
                    }
                }
                
            
                // TODO:
                // clear any bits we couldn't copy 
                // on account of being out of bounds?
                return gfx_result::success;    
            }
        };
    }
    // represents an in-memory bitmap
    template<typename PixelType>
    class bitmap final {
        size16 m_dimensions;
        uint8_t* m_begin;
    public:
        // the type of the bitmap, itself
        using type = bitmap<PixelType>;
        // the type of the pixel used for the bitmap
        using pixel_type = PixelType;
        using caps = gfx::gfx_caps< true,false,false,false,false,true,true>;
    private:
        gfx_result point_impl(point16 location,pixel_type rhs) {
            if(nullptr==begin()) {
                return gfx_result::out_of_memory;
            }
            // clip
            const size_t bit_depth = pixel_type::bit_depth;
            const size_t offs = (location.y*dimensions().width+location.x)*bit_depth;
            const size_t offs_bits = offs % 8;
            // now set the pixel
            uint8_t tmp[pixel_type::packed_size+(((int)pixel_type::pad_right_bits)<=offs_bits)];
            *((typename pixel_type::int_type*)tmp)=rhs.value();
            bits::shift_right(tmp,0,pixel_type::bit_depth+offs_bits,offs_bits);
            bits::set_bits(offs_bits,pixel_type::bit_depth,begin()+offs/8,tmp);
            return gfx_result::success;
        }
    public:
        // constructs a new bitmap with the specified size and buffer
        bitmap(size16 dimensions,void* buffer) : m_dimensions(dimensions),m_begin((uint8_t*)buffer) {}
        // constructs a new bitmap with the specified width, height and buffer
        bitmap(uint16_t width,uint16_t height,void* buffer) : m_dimensions(width,height),m_begin((uint8_t*)buffer) {}
        bitmap() : m_dimensions(0,0),m_begin(nullptr) {
            
        }
        bitmap(const type& rhs)=default;
        type& operator=(const type& rhs)=default;
        bitmap(type&& rhs)=default;
        type& operator=(type&& rhs) {
            m_dimensions = rhs.m_dimensions;
            m_begin = rhs.m_begin;
            return *this;
        }
        inline bool initialized() const {
            return nullptr!=m_begin;
        }
        gfx_result point(point16 location,pixel_type* out_pixel) const {
            if(nullptr==begin()) {
                return gfx_result::out_of_memory;
            }
            if(nullptr==out_pixel)
                return gfx_result::invalid_argument;
            if(location.x>=dimensions().width||location.y>=dimensions().height) {
                *out_pixel = pixel_type();
                return gfx_result::success;
            }
            const size_t bit_depth = pixel_type::bit_depth;
            const size_t offs = (location.y*dimensions().width+location.x)*bit_depth;
            const size_t offs_bits = offs % 8;
            uint8_t tmp[pixel_type::packed_size+(((int)pixel_type::pad_right_bits)<=offs_bits)];
            tmp[pixel_type::packed_size]=0;
            memcpy(tmp,begin()+offs/8,sizeof(tmp));
            if(0<offs_bits)
                bits::shift_left(tmp,0,sizeof(tmp)*8,offs_bits);
            pixel_type result;
            typename pixel_type::int_type r = 0;
            memcpy(&r,tmp,pixel_type::packed_size);
            r=helpers::order_guard(r);
            r&=pixel_type::mask;
            result.native_value=r;
            *out_pixel=result;
            return gfx_result::success;
        }
        gfx_result point(point16 location,pixel_type rhs) {
            if(nullptr==begin()) {
                return gfx_result::out_of_memory;
            }
            // clip
            if(location.x>=dimensions().width||location.y>=dimensions().height)
                return gfx_result::success;
            if(pixel_type::template has_channel_names<channel_name::A>::value) {
                pixel_type bgpx;
                gfx_result r=point(location,&bgpx);
                if(gfx_result::success!=r) {
                    return r;
                }
                if(!convert(rhs,&rhs,&bgpx)) {
                    return gfx_result::not_supported;
                }
            }
            return point_impl(location,rhs);
        }
        pixel_type point(point16 location) const {
            pixel_type result;
            point(location,&result);
            return result;
        }
        
        // indicates the dimensions of the bitmap
        inline size16 dimensions() const {
            return m_dimensions;
        }
        // provides a bounding rectangle for the bitmap, starting at 0,0
        inline rect16 bounds() const {
            return rect16(point16(0,0),dimensions());
        }
        // indicates the size of the bitmap, in pixels
        inline size_t size_pixels() const {
            return m_dimensions.height*m_dimensions.width;
        }
        // indicates the size of the bitmap in bytes
        inline size_t size_bytes() const {
            return (size_pixels()*((PixelType::bit_depth+7)/8));
        }
        // indicates the beginning of the bitmap buffer
        inline uint8_t* begin() const {
            return m_begin;
        }
        // indicates just past the end of the bitmap buffer
        inline uint8_t* end() const {
            return begin()+size_bytes();
        }
        // clears a region of the bitmap
        gfx_result clear(const rect16& dst) {
            if(nullptr==begin()) {
                return gfx_result::out_of_memory;
            }
            if(!dst.intersects(bounds())) return gfx_result::success;
            rect16 dstr = dst.crop(bounds());
            size_t dy = 0, dye=dstr.height();
            if(pixel_type::byte_aligned) {
                const size_t line_len = dstr.width()*pixel_type::packed_size;
                while(dy<dye) {
                   uint8_t* pdst = begin()+(((dstr.top()+dy)*dimensions().width+dstr.left())*pixel_type::packed_size); 
                   memset(pdst,0,line_len);
                   ++dy;
                }
            } else {
                // unaligned version
                const size_t line_len_bits = dstr.width()*pixel_type::bit_depth;
                while(dy<dye) {
                    const size_t offs = (((dstr.top()+dy)*dimensions().width+dstr.left())*pixel_type::bit_depth);
                    const size_t offs_bytes = offs / 8;
                    const size_t offs_bits = offs % 8;

                    uint8_t* pdst = begin()+offs_bytes;
                    bits::set_bits(pdst,offs_bits,line_len_bits,false);
                    ++dy;
                }
            }
            return gfx_result::success;
        }
        // fills a region of the bitmap with the specified pixel
        gfx_result fill(const rect16& dst,const pixel_type& pixel) {
            if(!dst.intersects(bounds())) return gfx_result::success;
            using pach = typename pixel_type::template channel_by_name_unchecked<channel_name::A>;
            using chindex = typename pixel_type::template channel_index_by_name<channel_name::A>;
            const size_t chi = chindex::value;
            if(pixel_type::template has_channel_names<channel_name::A>::value && pach::max!=pixel.template channel_unchecked<chi>()) {
                pixel_type bgpx;
                rect16 rc = dst.normalize();
                point16 pt;
                for(pt.y=rc.y1;pt.y<=rc.y2;++pt.y) {
                    for(pt.x=rc.x1;pt.x<=rc.x2;++pt.x) {
                        gfx_result r=point(pt,&bgpx);
                        if(gfx_result::success!=r) {
                            return r;
                        }
                        pixel_type dpx;
                        if(!convert(pixel,&dpx,&bgpx)) {
                            return gfx_result::not_supported;
                        }        
                        r=point_impl(pt,dpx);
                        if(gfx_result::success!=r) {
                            return r;
                        }
                    }    
                }
                return gfx_result::success;    
            } 
            if(nullptr==begin()) {
                return gfx_result::out_of_memory;
            }
            typename pixel_type::int_type be_val = pixel.value();
            rect16 dstr = dst.crop(bounds());
            size_t dy = 0, dye=dstr.height();
            if(pixel_type::byte_aligned) {
                const size_t line_len = dstr.width()*pixel_type::packed_size;
                while(dy<dye) {
                uint8_t* pdst = begin()+(((dstr.top()+dy)*dimensions().width+dstr.left())*pixel_type::packed_size); 
                for(size_t x = 0;x<line_len;x+=pixel_type::packed_size) {
                    
                    memcpy(pdst,&be_val,pixel_type::packed_size);
                    pdst+=pixel_type::packed_size;
                }
                ++dy;
                }
            } else if(pixel_type::bit_depth!=1) {
                // unaligned version
                uint8_t buf[pixel_type::packed_size+1];
                // set this to 9 to ensure no match on first iteration:
                size_t old_offs_bits=9;
                while(dy<dye) {
                    size_t dx = dstr.left();
                    while(dx<=dstr.right()) {
                        const size_t offs = (((dstr.top()+dy)*dimensions().width+dx)*pixel_type::bit_depth); 
                        const size_t offs_bits = offs % 8;
                        if(offs_bits!=old_offs_bits) {
                            memcpy(buf,&be_val,pixel_type::packed_size);
                            buf[pixel_type::packed_size]=0;
                            if(offs_bits!=0)
                                bits::shift_right(buf,0,sizeof(buf)*8,offs_bits);
                        }
                        const size_t offs_bytes = offs/8;
                        bits::set_bits(offs_bits,pixel_type::bit_depth,begin()+offs_bytes,buf);
                        ++dx;
                        old_offs_bits=offs_bits;
                    }
                    ++dy;
                }
            } else { // monochrome specialization
                const size_t line_len_bits = dstr.width()*pixel_type::bit_depth;
                bool set = 0!=pixel.native_value;
                while(dy<dye) {
                    const size_t offs = (dstr.top()+dy)*dimensions().width+dstr.left()*pixel_type::bit_depth;
                    uint8_t* pdst = begin()+(offs/8);
                    bits::set_bits(pdst,offs%8,line_len_bits,set);
                    ++dy;
                }
            } 
        
            return gfx_result::success;
        }
        template<typename Destination>
        inline gfx_result copy_to(const rect16& src_rect,Destination& dst,point16 location) const {
            if(nullptr==begin() || nullptr==dst.begin())
                return gfx_result::out_of_memory;
            if(!src_rect.intersects(bounds())) return gfx_result::success;
            rect16 srcr = src_rect.crop(bounds());
            rect16 dstr= rect16(location,srcr.dimensions()).crop(dst.bounds());
            srcr=rect16(srcr.location(),dstr.dimensions());
            return helpers::bmp_copy_to_helper<type,Destination,!(pixel_type::template has_channel_names<channel_name::A>::value)>::copy_to(*this,srcr,dst,dstr);
        }
        // computes the minimum required size for a bitmap buffer, in bytes
        constexpr inline static size_t sizeof_buffer(size16 size) {
            return (size.width*size.height*pixel_type::bit_depth+7)/8;
        }
        // computes the minimum required size for a bitmap buffer, in bytes
        constexpr inline static size_t sizeof_buffer(uint16_t width,uint16_t height) {
            return sizeof_buffer(size16(width,height));
        }
        // this check is already guaranteed by asserts in the pixel itself
        // this is more so it won't compile unless PixelType is actually a pixel
        static_assert(PixelType::channels>0,"The type is not a pixel or the pixel is invalid");
    };
    template<typename PixelType>
    class large_bitmap final {
        size16 m_dimensions;
        uint16_t m_segment_height;
        bitmap<PixelType>* m_segments;
        void(*m_deallocate)(void* ptr);
        void deinit(size_t count) {
            if(nullptr==m_segments) {
                return;
            }
            if(0==count) {
                const uint16_t remainder = m_dimensions.height%m_segment_height;
                const size_t segment_count = (m_dimensions.height/m_segment_height)+(!!remainder);
                count = segment_count;
            }
            for(size_t j=0;j<count;++j) {
                m_deallocate(m_segments[j].begin());
            }
            m_deallocate(m_segments);
            m_segments = nullptr;
        }

    public:
        using type = large_bitmap<PixelType>;
        using pixel_type = PixelType;
        using caps = gfx_caps<false,false,false,false,false,false,true>;
        using segment_type = bitmap<pixel_type>;
        large_bitmap(size16 dimensions,uint16_t segment_height, void*(allocate)(size_t)=::malloc,void(deallocate)(void*)=::free) 
            : m_dimensions(dimensions),m_segment_height(segment_height),m_deallocate(deallocate)
        {
            
            if(m_segment_height==0)
                m_segment_height=1;
            if(m_segment_height>m_dimensions.height)
                m_segment_height = m_dimensions.height;
            if(m_deallocate==nullptr) m_deallocate = ::free;
            if(allocate==nullptr) allocate = ::malloc;
            const size16 segment_size(m_dimensions.width,m_segment_height);
            const uint16_t remainder = m_dimensions.height%m_segment_height;
            const size_t segment_count = (m_dimensions.height/m_segment_height)+(!!remainder);
            m_segments = (segment_type*)allocate(sizeof(segment_type)*segment_count);
            if(nullptr==m_segments) {
                return;
            }
            size_t i=0;
            segment_type* p=m_segments;
            for(;i<segment_count;++i) {
                size16 s = segment_size;
                if(i==segment_count-1 && !!remainder) {
                    s = size16(s.width,remainder);
                }
                *p++=segment_type(s,allocate(segment_type::sizeof_buffer(s)));
                if(nullptr==p->begin()) {
                    // free everything we allocated so far
                    deinit(i);
                    return;
                }
            }
        }
        large_bitmap() : m_dimensions(0,0),m_segment_height(0),m_segments(nullptr) {
            
        }
        large_bitmap(const type& rhs)=delete;
        type& operator=(const type& rhs)=delete;
        large_bitmap(type&& rhs) : m_dimensions(rhs.m_dimensions),m_segment_height(rhs.m_segment_height),m_segments(rhs.m_segments) {
            rhs.m_dimensions = size16(0,0);
            rhs.m_segment_height = 0;
            rhs.m_segments = nullptr;
        }
        type& operator=(type&& rhs) {
            m_dimensions=rhs.m_dimensions;
            m_segment_height=rhs.m_segment_height;
            m_segments=rhs.m_segments;
            rhs.m_dimensions = size16(0,0);
            rhs.m_segment_height = 0;
            rhs.m_segments = nullptr;
            return *this;
        }
        ~large_bitmap() {
            deinit(0);
        }
        inline bool initialized() const {
            return nullptr!=m_segments;
        }
        inline size16 dimensions() const {
            return m_dimensions;
        }
        inline rect16 bounds() const {
            return m_dimensions.bounds();
        }
        gfx_result point(point16 location,pixel_type color) {
            if(nullptr==m_segments) {
                return gfx_result::out_of_memory;
            }
            if(location.x>=m_dimensions.width || location.y>=m_dimensions.height) {
                return gfx_result::success;
            }
            const size_t seg = location.y/m_segment_height;
            const uint16_t offs = location.y%m_segment_height;
            return m_segments[seg].point(point16(location.x,offs),color);
        }
        gfx_result point(point16 location,pixel_type* out_color) const {
            if(nullptr==m_segments) {
                return gfx_result::out_of_memory;
            }
            pixel_type result;
            if(location.x>=m_dimensions.width || location.y>=m_dimensions.height) {
                *out_color=result;
                return gfx_result::success;
            }
            const size_t seg = location.y/m_segment_height;
            const uint16_t offs = location.y%m_segment_height;
            return m_segments[seg].point(point16(location.x,offs),out_color);
        }
        gfx_result fill(const rect16& bounds,pixel_type color) {
            if(nullptr==m_segments) {
                return gfx_result::out_of_memory;
            }
            const rect16 tb = this->bounds();
            const rect16 b = bounds.normalize();
            if(!b.intersects(tb)) {
                return gfx_result::success;
            }
            const size_t segment = b.y1/m_segment_height;
            const uint16_t offset = b.y1%m_segment_height;
            rect16 rf(b.x1,offset,b.x2,m_segment_height-1);
            uint16_t h = (b.y2-b.y1+1);
            if(m_segment_height>=h) {
                // it's all contained within one segment
                rf.y2 = h+offset-1;
                return m_segments[segment].fill(rf,color);
            }
            gfx_result r=m_segments[segment].fill(rf,color);
            if(gfx_result::success!=r) {
                return r;
            }
            rf.y1=0;
            size_t i = 1;
            for(;b.y2>=i*m_segment_height;++i) {
                gfx_result r=m_segments[i].fill(rf,color);
                if(gfx_result::success!=r) {
                    return r;
                }   
            }
            // see if there's any remainder to fill
            const uint16_t yy2 = (b.y2+b.y1);
            const uint16_t end_offset = yy2%m_segment_height;
            if(0!=end_offset) {
                const size_t end_segment = yy2/m_segment_height;
                rf.y2 = end_offset;
                return m_segments[end_segment].fill(rf,color);
            }
            return gfx_result::success;
        }
        gfx_result clear(const rect16& bounds) {
            pixel_type p;
            p.native_value = 0;
            return fill(bounds,p);
        }
    };
    

    
}

#endif