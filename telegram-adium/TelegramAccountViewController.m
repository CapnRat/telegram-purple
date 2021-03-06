/* 
 * Adium is the legal property of its developers, whose names are listed in the copyright file included
 * with this source distribution.
 * 
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with this program; if not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#import "TelegramAccountViewController.h"
#import "TelegramAccount.h"

#import <Adium/AIService.h>
#import <AIUtilities/AIStringFormatter.h>
#import <AIUtilities/AIAttributedStringAdditions.h>
#import <AIUtilities/AIPopUpButtonAdditions.h>

#include "telegram-purple.h"

@implementation TelegramAccountViewController

- (NSString *)nibName{
    return @"TelegramAccountView";
}

- (void)configureForAccount:(AIAccount *)inAccount
{
  [super configureForAccount:inAccount];
  
  NSString *acceptSecretChats = [account
                                 preferenceForKey:@"Telegram:"TGP_KEY_ACCEPT_SECRET_CHATS
                                 group:GROUP_ACCOUNT_STATUS] ?: @TGP_DEFAULT_ACCEPT_SECRET_CHATS;
  
  NSInteger row = 0;
  if ([acceptSecretChats isEqual:@"always"]) {
    row = 1;
  } else if ([acceptSecretChats isEqual:@"never"]) {
    row = 2;
  }
  [radio_Encryption selectCellAtRow:row column:0];
  
  id s = [account preferenceForKey:@"Telegram:"TGP_KEY_HISTORY_SYNC_ALL group:GROUP_ACCOUNT_STATUS];
  [checkbox_historySyncAll setState:[s boolValue]];
  
  id s4 = [account preferenceForKey:@"Telegram:"TGP_KEY_DISPLAY_READ_NOTIFICATIONS group:GROUP_ACCOUNT_STATUS];
  [checkbox_displayReadNotifications setState:[s4 boolValue]];

  NSString *inactiveDaysOffline = [account
                                   preferenceForKey:@"Telegram:"TGP_KEY_INACTIVE_DAYS_OFFLINE
                                   group:GROUP_ACCOUNT_STATUS] ?: @"";
  [textField_inactiveDaysOffline setStringValue:inactiveDaysOffline];
  
  NSString *historyRetrievalThreshold = [account
                                         preferenceForKey:@"Telegram:"TGP_KEY_HISTORY_RETRIEVAL_THRESHOLD
                                         group:GROUP_ACCOUNT_STATUS] ?: @"";
  [textField_historyRetrieveDays setStringValue:historyRetrievalThreshold];
}

- (void)saveConfiguration
{
	[super saveConfiguration];
	
  NSArray *selections = @[@"ask", @"always", @"never"];
  
  [account setPreference:selections[[radio_Encryption selectedRow]]
                  forKey:@"Telegram:"TGP_KEY_ACCEPT_SECRET_CHATS
                   group:GROUP_ACCOUNT_STATUS];

  [account setPreference:[NSNumber numberWithBool:[checkbox_historySyncAll state]]
                  forKey:@"Telegram:"TGP_KEY_HISTORY_SYNC_ALL
                   group:GROUP_ACCOUNT_STATUS];
  
  [account setPreference:[NSNumber numberWithBool:[checkbox_displayReadNotifications state]]
                  forKey:@"Telegram:"TGP_KEY_DISPLAY_READ_NOTIFICATIONS
                   group:GROUP_ACCOUNT_STATUS];
  
  [account setPreference:[textField_historyRetrieveDays stringValue]
                  forKey:@"Telegram:"TGP_KEY_HISTORY_RETRIEVAL_THRESHOLD
                   group:GROUP_ACCOUNT_STATUS];
  
  [account setPreference:[textField_inactiveDaysOffline stringValue]
                  forKey:@"Telegram:"TGP_KEY_INACTIVE_DAYS_OFFLINE
                   group:GROUP_ACCOUNT_STATUS];
}	

@end
