#include "CadViewport.h"

void CadViewport::reportGeometryFailure(const QString& operation,const QString& reason,bool popup) {
    if(commandSession_) commandSession_->geometryError=reason;
    emit statusMessage(tr("%1 failed: %2 Original geometry was kept.").arg(operation,reason));
    if(popup) emit geometryOperationFailed(operation,reason);
}
