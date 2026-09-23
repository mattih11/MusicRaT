#pragma once

#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/control_events.hpp>
#include <musicrat/protocol/control_mapping.hpp>
#include <musicrat/protocol/deck_control.hpp>
#include <musicrat/protocol/file_player.hpp>
#include <musicrat/protocol/note_events.hpp>
#include <musicrat/protocol/parameter_events.hpp>
#include <musicrat/protocol/parameter_state.hpp>
#include <musicrat/protocol/oscillator.hpp>
#include <musicrat/protocol/playback_status.hpp>
#include <musicrat/protocol/telemetry.hpp>
#include <musicrat/protocol/transport.hpp>
#include <musicrat/protocol/wav_sink.hpp>

#include <commrat/commrat.hpp>

using AudioBlockOutput = commrat::DataWithCommands<
    CommRaT::Messages::AudioBlock,
    CommRaT::Messages::ResetPhase,
    CommRaT::Messages::LoadMedia,
    CommRaT::Messages::SeekMedia,
    CommRaT::Messages::UnloadMedia>;
using ParameterEventOutput = commrat::DataWithCommands<
    CommRaT::Messages::ParameterEventBlock>;
using NoteEventOutput = commrat::DataWithCommands<
    CommRaT::Messages::NoteEventBlock>;
using DeckControlEventOutput = commrat::DataWithCommands<
    CommRaT::Messages::DeckControlEventBlock>;
using LevelMeterOutput = commrat::DataWithCommands<
    CommRaT::Messages::LevelMeterBlock>;
using PlaybackStatusOutput = commrat::DataWithCommands<
    CommRaT::Messages::PlaybackStatusBlock>;
using TransportOutput = commrat::DataWithCommands<
    CommRaT::Messages::TransportBlock>;
using ControlEventOutput = commrat::DataWithCommands<
    CommRaT::Messages::ControlEventBlock>;
using ParameterStateOutput = commrat::DataWithCommands<
    CommRaT::Messages::ParameterStateBlock>;

using MusicRaT = commrat::CommRaT<
    AudioBlockOutput,
    ParameterEventOutput,
    NoteEventOutput,
    DeckControlEventOutput,
    LevelMeterOutput,
    PlaybackStatusOutput,
    TransportOutput,
    ControlEventOutput,
    ParameterStateOutput>;