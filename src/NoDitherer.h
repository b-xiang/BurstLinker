//
// Created by succlz123 on 2017/11/15.
//

#ifndef BURSTLINKER_NODITHERER_H
#define BURSTLINKER_NODITHERER_H


#include "Ditherer.h"


class NoDitherer : public Ditherer {

public:

    void
    dither(uint32_t *originalColors, int width, int height,
           uint8_t *quantizerColors, int quantizerSize);

};

#endif //BURSTLINKER_NODITHERER_H
