/*
* copyright (c) 2010 Sveriges Television AB <info@casparcg.com>
*
*  This file is part of CasparCG.
*
*    CasparCG is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    CasparCG is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.

*    You should have received a copy of the GNU General Public License
*    along with CasparCG.  If not, see <http://www.gnu.org/licenses/>.
*
*/
#include "../stdafx.h"

#include "write_frame.h"

#include "gpu/ogl_device.h"
#include "gpu/host_buffer.h"
#include "gpu/device_buffer.h"

#include <core/producer/frame/frame_visitor.h>
#include <core/producer/frame/pixel_format.h>

namespace caspar { namespace core {
																																							
struct write_frame::implementation
{				
	ogl_device&										ogl_;
	std::vector<std::shared_ptr<host_buffer>>		buffers_;
	std::array<std::shared_ptr<device_buffer>, 4>	textures_;
	std::vector<int16_t>							audio_data_;
	const core::pixel_format_desc					desc_;
	int												tag_;
	core::video_mode::type							mode_;

	implementation(ogl_device& ogl, int tag, const core::pixel_format_desc& desc) 
		: ogl_(ogl)
		, desc_(desc)
		, tag_(tag)
		, mode_(core::video_mode::progressive)
	{
		ogl_.invoke([&]
		{
			std::transform(desc.planes.begin(), desc.planes.end(), std::back_inserter(buffers_), [&](const core::pixel_format_desc::plane& plane)
			{
				return ogl_.create_host_buffer(plane.size, host_buffer::write_only);
			});
		}, high_priority);
	}
			
	void accept(write_frame& self, core::frame_visitor& visitor)
	{
		visitor.begin(self);
		visitor.visit(self);
		visitor.end();
	}

	boost::iterator_range<uint8_t*> image_data(size_t index)
	{
		if(index >= buffers_.size() || !buffers_[index]->data())
			return boost::iterator_range<const uint8_t*>();
		auto ptr = static_cast<uint8_t*>(buffers_[index]->data());
		return boost::iterator_range<uint8_t*>(ptr, ptr+buffers_[index]->size());
	}

	const boost::iterator_range<const uint8_t*> image_data(size_t index) const
	{
		if(index >= buffers_.size() || !buffers_[index]->data())
			return boost::iterator_range<const uint8_t*>();
		auto ptr = static_cast<const uint8_t*>(buffers_[index]->data());
		return boost::iterator_range<const uint8_t*>(ptr, ptr+buffers_[index]->size());
	}

	void commit()
	{
		for(size_t n = 0; n < buffers_.size(); ++n)
			commit(n);
	}

	void commit(size_t plane_index)
	{
		if(plane_index >= buffers_.size())
			return;
				
		auto buffer = std::move(buffers_[plane_index]); // Release buffer once done.

		if(!buffer)
			return;
		
		ogl_.begin_invoke([=]
		{
			auto plane = desc_.planes[plane_index];
			textures_[plane_index] = ogl_.create_device_buffer(plane.width, plane.height, plane.channels);			
			textures_[plane_index]->read(*buffer);
		}, high_priority);
	}
};
	
write_frame::write_frame(ogl_device& ogl, int32_t tag, const core::pixel_format_desc& desc) 
	: impl_(new implementation(ogl, tag, desc)){}
write_frame::write_frame(const write_frame& other) : impl_(new implementation(*other.impl_)){}
void write_frame::accept(core::frame_visitor& visitor){impl_->accept(*this, visitor);}

boost::iterator_range<uint8_t*> write_frame::image_data(size_t index){return impl_->image_data(index);}
std::vector<int16_t>& write_frame::audio_data() { return impl_->audio_data_; }
const boost::iterator_range<const uint8_t*> write_frame::image_data(size_t index) const
{
	return boost::iterator_range<const uint8_t*>(impl_->image_data(index).begin(), impl_->image_data(index).end());
}
const boost::iterator_range<const int16_t*> write_frame::audio_data() const
{
	return boost::iterator_range<const int16_t*>(impl_->audio_data_.data(), impl_->audio_data_.data() + impl_->audio_data_.size());
}
int write_frame::tag() const {return impl_->tag_;}
const core::pixel_format_desc& write_frame::get_pixel_format_desc() const{return impl_->desc_;}
const std::vector<safe_ptr<device_buffer>> write_frame::get_textures() const
{
	std::vector<safe_ptr<device_buffer>> textures;
	BOOST_FOREACH(auto texture, impl_->textures_)
	{
		if(texture)
			textures.push_back(make_safe(texture));
	}

	return textures;
}
void write_frame::commit(size_t plane_index){impl_->commit(plane_index);}
void write_frame::commit(){impl_->commit();}
void write_frame::set_type(const video_mode::type& mode){impl_->mode_ = mode;}
core::video_mode::type write_frame::get_type() const{return impl_->mode_;}

}}