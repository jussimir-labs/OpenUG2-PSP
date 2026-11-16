/* EventIndicator's SMS phases (GameCube EngageEvent::updateState): phone
 * for one second, the mapped command until four seconds, then unread count.
 * A higher-priority street prompt settles the notification before taking
 * the shared backing; returning from it must not announce old mail again. */
#ifndef NFSU2_SMS_INDICATOR_H
#define NFSU2_SMS_INDICATOR_H
typedef struct { int unread,latest;float age; } SmsIndicator;
enum { SMS_IND_OFF, SMS_IND_PHONE=2, SMS_IND_COMMAND, SMS_IND_STATIC };
static inline int sms_indicator_step(SmsIndicator *s,int unread,int latest,
                                     int overridden,int stage0,float dt) {
    if(unread<=0){s->unread=0;s->latest=-1;s->age=0;return SMS_IND_OFF;}
    if(!s->unread || unread>s->unread || (unread==s->unread && latest!=s->latest))s->age=0;
    s->unread=unread;s->latest=latest;
    if(overridden)s->age=4.f;
    else if(dt>0 && !(stage0 && s->age<1.f))s->age+=dt;
    return s->age<1.f?SMS_IND_PHONE:s->age<4.f?SMS_IND_COMMAND:SMS_IND_STATIC;
}
#endif
