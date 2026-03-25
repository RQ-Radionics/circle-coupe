// Parte de SimCoupe - Un emulador de SAM Coupe
//
// FrameBufferBackup.h: Interfaz para guardar y restaurar el framebuffer
//
// Copyright (c) 2023 Tu Nombre
//
// Este programa es software libre; puedes redistribuirlo y/o modificarlo
// bajo los términos de la Licencia Pública General de GNU publicada por
// la Free Software Foundation; ya sea la versión 2 de la Licencia, o
// (a tu elección) cualquier versión posterior.
//
// Este programa se distribuye con la esperanza de que sea útil,
// pero SIN NINGUNA GARANTÍA; sin siquiera la garantía implícita de
// COMERCIABILIDAD o IDONEIDAD PARA UN PROPÓSITO PARTICULAR. Consulta la
// Licencia Pública General de GNU para más detalles.
//
// Deberías haber recibido una copia de la Licencia Pública General de GNU
// junto con este programa; si no, escribe a la Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

#pragma once

#include "FrameBuffer.h"

namespace FrameBufferBackup
{

void Save(const FrameBuffer& fb);
void Restore(FrameBuffer& fb);
bool HasBackup();
void Clear();

}