/*
 * nxplay - GStreamer-based media playback library
 *
 * Copyright (C) 2015 by Carlos Rafael Giani < dv AT pseudoterminal DOT org >
 *
 * Distributed under the Boost Software License, Version 1.0. See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt .
 */

/** @file */

#ifndef NXPLAY_PIPELINE_HPP
#define NXPLAY_PIPELINE_HPP

#include <string>
#include <gst/gst.h>
#include <gst/audio/streamvolume.h>
#include "media.hpp"


/** nxplay */
namespace nxplay
{


/// Pipeline states
/**
 * Some of these states are transitional. pipeline::is_transitioning() returns true if
 * called during one of these states. Certain calls like pipeline::play_media() or
 * pipeline::set_current_position() will internally be postponed until the transitional
 * state has finished.
 */
enum states
{
	/// Pipeline is idling. No media is loaded, no devices are acquired.
	state_idle,
	/// Pipeline is starting. This state is transitional, and will switch to state
	/// paused/playing when done.
	state_starting,
	/// Pipeline is stopping. This state is transitional, and will switch to state
	/// idle when done.
	state_stopping,
	/// Pipeline is seeking in the currently media.
	/// This state is transitional; it will remain until seeking is complete.
	/// Afterwards, it will return to the previous paused/playing state.
	state_seeking,
	/// Pipeline is buffering the current media.
	/// This state is transitional; it will remain until buffering is complete.
	/// Afterwards, it will return to the previous paused/playing state.
	state_buffering,
	/// Pipeline is playing the current media.
	state_playing,
	/// Pipeline is paused.
	state_paused
};


/// Positioning units.
/**
 * These are needed for duration updates and playback position requests. nxplay supports
 * two ways of specifying position and duraiton: nanoseconds (the GStreamer timestamp unit)
 * and bytes. Some media might not support them both. If for example bytes are not supported,
 * duration updates in bytes and position queries in bytes will always return -1.
 */
enum position_units
{
	position_unit_nanoseconds,
	position_unit_bytes,
};


/// Returns a string representation of a state; useful for logging
std::string get_state_name(states const p_state);


/// Abstract pipeline interface.
/**
 * This is the core interface in nxplay. Through this interface, playback is started and
 * controlled.
 *
 * Pipelines get media objects to play or schedule as next playback. Derived classes are
 * however free to not support next media. One example would be a pipeline which acts as
 * a fixed receiver of some kind. A "next media" makes no sense there.
 *
 * If something goes wrong, pipelines reinitialize themselves. If a fatal error occurs
 * which cannot be fixed even by reinitialization, pipelines are allowed to throw
 * exceptions based on std::exception . Exceptions which cross the boundaries of the
 * pipeline are only allowed for this purpose. Otherwise, the public pipeline methods
 * never throw exceptions.
 *
 * Method calls are only rejected if there is absolutely no other way. If for example
 * the pipeline is in a transitional state (see the states enum for details), and
 * a play_media() call cannot be performed right then, this call must somehow be
 * internally recorded and postponed until the transition is finished. Rejections are
 * only permitted if the request fails for some reason (for example, when the media
 * URI points to a non-existing source).
 *
 * Transitional states exist because pipelines are free to do state changes asynchronously.
 * For example, it is not required (and generally not recommended) to block inside
 * a set_current_position() call until the pipeline finished seeking. Instead, callers
 * should make use of any notification mechanisms the derived pipeline implementation
 * offer (for example, main_pipeline uses callback functions). Transitional states allow
 * the caller to display some sort of waiting indicator in the user interface.
 *
 * Unless there is a very good reason to do so, pipeline implementations do not allow to
 * directly set the state from the outside, since this can lead to many undefined cases.
 *
 * The fundamental goal is to make the pipeline robust and simple to use. It must be
 * able to handle any requests without deadlocking, reaching some undefined state, or
 * requiring multiple manual steps for a request to succeeed. For example, if something
 * is currently playing, it must not be necessary to manually call stop() prior to the
 * play_media() call, or call set_paused() before and after a set_current_position()
 * call. Any public method can be called at any time in any state until explicitely
 * stated otherwise.
 *
 * In most cases, the derived main_pipeline class will be used. The pipeline base class
 * is useful as a building block to a "selector" however which can switch between
 * pipelines. This is planned for future nxplay versions.
 *
 * Unless documented otherwise, pipeline reinitializations always cancel any internal
 * postponed tasks.
 *
 * The methods in general are not guaranteed to be thread safe.
 */
class pipeline
{
public:
	/// Destructor. Cancels any current transitions and ends playback immediately.
	virtual ~pipeline();

	/// Begins playback of given media, either right now, or when the current playback ends.
	/**
	 * This function instructs the pipeline to commence playing the given media.
	 * If p_play_now is true, or if the current playback's token is the same as p_token
	 * (explained in detail below), or if no playback is currently running, p_media is
	 * played immediately, and becomes the "current media". Otherwise, p_media is scheduled
	 * to become the "next media", and is played as soon as the current media ends. This
	 * makes it possible for pipeline implementations to support gapless playback. If
	 * some other media has already been scheduled as next media earlier, then this new
	 * next media replaces it.
	 *
	 * If media cannot currently be played because the pipeline is in a transitional state,
	 * the call is postponed, and automatically executed as soon as the transition is finished.
	 * If it is postponed, the return value is still true.
	 *
	 * The call is also given a token. A token is a method to identify unique calls and
	 * prevent certain otherwise ambiguous cases. Example: user wants to play X now,
	 * calls play_media(X, true), and wants to play Y afterwards, thus calls play_media(Y, false).
	 * But then, *before* X ends, the user changes his mind, and wants to play Z instead of Y
	 * after X ends. If the user is quick enough, the play_media(Z, false); call will
	 * overwrite the previous "next media"; it will replace Y with Z. If however the user
	 * is not fast enough, and Y starts playing, the play_media(Z, false); call will schedule
	 * Z to be played after Y.
	 * To counter this, tokens are used. With tokens, this situation is resolved. The user
	 * then simply reuses the token he used for the play_media(Y, false); call. Example:
	 * play_media(1, X, true) -> play_media(2, Y, false) -> play_media(2, Z, false) .
	 * If the user is not fast enough, and Y starts playing, the last play_media() call
	 * unambiguously tells the pipeline that Z is replacing Y. Therefore, in this case, Y
	 * will immediately stop, and Z will start playing.
	 *
	 * If playback starts right now, any previously set next media gets discarded.
	 *
	 * Token numbers can in theory be anything, as long as they are assigned properly, just like
	 * the example above demonstrates. For convenience, the get_new_token() function can be used,
	 * which generates unique tokens.
	 *
	 * @note Derived classes typically don't override the play_media() overloads. Instead, they
	 * override the protected play_media_impl() function.
	 *
	 * @param p_token Token to associate the playback request with
	 * @param p_media Media to play (either now or later); the media object is copied internally
	 * @param p_play_now If true, the media must be played right now (see above)
	 * @return true if the request succeeded, false otherwise (a postponed playback request
	 *         still returns true!)
	 */
	virtual bool play_media(guint64 const p_token, media const &p_media, bool const p_play_now);
	/// Overlaoded play_media() function for movable media obejcts
	/**
	 * The only difference to the other overload is that p_media is is an rvalue reference.
	 * Useful to avoid temporary media object copies (this includes their payloads).
	 */
	virtual bool play_media(guint64 const p_token, media &&p_media, bool const p_play_now);
	/// Stops any current playback and erases any scheduled next media.
	/**
	 * If this is called in the idle state, nothing happens. Otherwise, the pipeline will be
	 * put to the idle state. Any present current/next media will be erased. Any internal
	 * playback pipelines will be shut down. If the pipeline is in a transitional state and
	 * thus cannot be stopped immediately, the call is postponed, and the pipeline stopped
	 * as soon as the transition finishes.
	 */
	virtual void stop() = 0;
	/// Convenience function, useful for play_media() calls.
	/**
	 * @return Newly generated unique tokens
	 */
	virtual guint64 get_new_token() = 0;

	/// Pauses/unpauses the pipeline.
	/**
	 * This call is only meaningful if the pipeline is either in the playing or paused state
	 * or is transitioning to one of these two states. Otherwise, it is ignored. If the
	 * pipeline is already paused, and p_paused is true, the call is ignored. Same if
	 * the pipeline is playing, and p_paused is false.
	 *
	 * In the special transitioning case described earlier where the pipeline is transitioning
	 * to either the paused or the playing state, this call is postponed, and executed
	 * once the transition is finished.
	 *
	 * @param p_paused If true, this initiates a state change to state_paused, otherwise
	 *        it initiates a state change to state_playing (see above for exceptions to this rule)
	 */
	virtual void set_paused(bool const p_paused) = 0;

	/// Returns true if the  pipeline is currently in a transitioning state.
	/**
	 * A transitioning state is a state where certain actions like play_media() cannot
	 * be executed immediately. See the states enum for details.
	 *
	 * @return true if the pipeline is in a transitional state, false otherwise
	 */
	virtual bool is_transitioning() const = 0;

	/// Returns the state the pipeline is currently in.
	virtual states get_current_state() const = 0;

	/// Sets the pipeline's current playback position (also known as "seeking").
	/**
	 * This call is ignored unless the pipeline is in a paused or playing state, or
	 * transitioning to one of these two states.
	 *
	 * This call is postponed if the pipeline is in a transitional state, and executed
	 * as soon as the transition ends. Pipelines do not have to support seeking, and can
	 * ignore this call if they don't, since seeking may not be supported with certain
	 * media (for example, RTSP or HTTP radio streams). Some media might also only
	 * support byte seeks, or nanosecond seeks (in practice, the latter is supported by
	 * pretty much all types of media that can seek in general, so it is a safe bet to
	 * use it).
	 *
	 * Seeking may occur asynchronously in the background. In this case, the pipeline
	 * state switches to state_seeking, and back to the original state (either playing
	 * or paused) when seeking is done.
	 *
	 * @param p_new_position New position, either in nanoseconds or in bytes, depending on p_unit
	 * @param p_unit Unit for the position value
	 */
	virtual void set_current_position(gint64 const p_new_position, position_units const p_unit = position_unit_nanoseconds) = 0;
	/// Returns the current position in the given units.
	/**
	 * @param p_unit Units to use for the current position
	 * @return The current position in the given units, or -1 if the current position cannot
	 *         be determined (at least not with the given unit)
	 */
	virtual gint64 get_current_position(position_units const p_unit = position_unit_nanoseconds) const = 0;

	/// Returns the current duration in the given units.
	/**
	 * @param p_unit Units to use for the current duration
	 * @return The current duration in the given units, or -1 if the current duration cannot
	 *         be determined (at least not with the given unit)
	 */
	virtual gint64 get_duration(position_units const p_unit = position_unit_nanoseconds) const = 0;

	/// Sets the current volume, with the given format.
	/**
	 * See the GStreamer documentation for GstStreamVolume for details about the format.
	 * If nothing inside the pipeline supports volume, this call is ignored.
	 *
	 * @param p_new_volume New volume to use
	 * @param p_format Format of the new volume to use
	 */
	virtual void set_volume(double const p_new_volume, GstStreamVolumeFormat const p_format = GST_STREAM_VOLUME_FORMAT_LINEAR) = 0;
	/// Retrieves the current volume in the given format.
	/**
	 * If nothing inside the pipeline supports volume, this call returns 1.0.
	 *
	 * @param p_format Required format for the return value
	 * @return Current volume, or 1.0 if volume is not supported by the pipeline
	 */
	virtual double get_volume(GstStreamVolumeFormat const p_format = GST_STREAM_VOLUME_FORMAT_LINEAR) const = 0;
	/// Mutes/unmutes the audio playback
	/**
	 * If nothing inside the pipeline supports muting, this call is ignored.
	 *
	 * @param p_mute true if audio shall be muted
	 */
	virtual void set_muted(bool const p_mute) = 0;
	/// Determines if audio playback is currently muted or not.
	/**
	 * If nothing inside the pipeline supports muting, this call return false.
	 *
	 * @return true if audio playback is currently muted, false otherwise
	 */
	virtual bool is_muted() const = 0;


protected:
	// Derived classes only need to overload this one, and can leave the two play_media() functions alone
	virtual bool play_media_impl(guint64 const p_token, media &&p_media, bool const p_play_now) = 0;
};


} // namespace nxplay end


#endif