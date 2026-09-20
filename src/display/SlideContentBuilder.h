#pragma once

#include "SlideContent.h"
#include "core/ContentItem.h"

// Shared by PresentationController (what's live) and the "Предпросмотр"
// action (what a slide would look like without going live). `loopVideo`
// only matters for ContentType::Video (see SlideContent::videoLoop).
SlideContent buildSlideContent(const ContentItem &item, const QStringList &slides, int slideIndex, bool loopVideo = false);
