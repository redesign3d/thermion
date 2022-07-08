/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#import <UIKit/UIKit.h>
#include "FilamentViewer.hpp"
NS_ASSUME_NONNULL_BEGIN

/**
 * FILModelView is simply a UIView with an OpenGL layer.
 *
 *
 */
@interface FilamentView : UIView
- (void)setViewer:(polyvox::FilamentViewer*)viewer;
- (void)startDisplayLink;
- (void)stopDisplayLink;
@end

NS_ASSUME_NONNULL_END
