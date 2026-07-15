/*
  quickinspector_types.h — minimal enum definitions matching the probe's
  QuickInspectorInterface, for use with Endpoint::invokeObject.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later

  The bridge cannot include the real quickinspectorinterface.h (not in installed
  headers — see PLAN.md open question #1). But setCustomRenderMode() takes a
  RenderMode enum by value, and Endpoint::invokeObject serializes QVariant args
  via QDataStream which uses the QVariant typeName() for custom types. So we
  need a type whose fully-qualified name matches "GammaRay::QuickInspectorInterface::RenderMode"
  exactly, and which is registered via Q_DECLARE_METATYPE on both sides.

  This header defines just that — a nested enum inside a class with the same
  namespace/class structure. Q_DECLARE_METATYPE registers it with the matching
  type name, enabling cross-process QVariant serialization.
*/

#ifndef QUICKINSPECTOR_TYPES_H
#define QUICKINSPECTOR_TYPES_H

#include <QMetaType>
#include <QObject>

namespace GammaRay {

// Minimal stand-in for the real QuickInspectorInterface — just the RenderMode
// enum. Do NOT add Q_OBJECT (we don't need MOC for this header-only class;
// Q_DECLARE_METATYPE below handles metatype registration).
class QuickInspectorInterface
{
public:
    // Values MUST match the probe's enum (quickinspectorinterface.h:52-66).
    enum RenderMode
    {
        NormalRendering,
        VisualizeClipping,
        VisualizeOverdraw,
        VisualizeBatches,
        VisualizeChanges,
        VisualizeTraces,
    };
};

} // namespace GammaRay

Q_DECLARE_METATYPE(GammaRay::QuickInspectorInterface::RenderMode)

#endif // QUICKINSPECTOR_TYPES_H
