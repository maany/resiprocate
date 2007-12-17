#if !defined(RESIP_HELPER_HXX)
#define RESIP_HELPER_HXX 

#include <time.h>

#include "resiprocate/Symbols.hxx"
#include "resiprocate/Uri.hxx"
#include "resiprocate/MethodTypes.hxx"
#include "resiprocate/os/BaseException.hxx"

namespace resip
{

class SipMessage;
class NameAddr;
class SecurityAttributes;
class Security;

class UnsupportedAuthenticationScheme : public BaseException
{
   public:
      UnsupportedAuthenticationScheme(const Data& msg, const Data& file, const int line)
         : BaseException(msg, file, line) {}
      
      const char* name() const { return "UnsupportedAuthenticationScheme"; }
};

class Helper
{
   public:

      const static int tagSize;  //bytes in to-tag& from-tag, should prob. live
      //somewhere else

      // Sorry if this doesn't build in Win32, let me know  (jf)
      template<typename T>
      static T aBitSmallerThan(T secs)
      {
         return resipMax(T(0), resipMin(T(secs-5), T(9*secs/10)));
      }

      // e.g. to jitter the expires in a SUBSCRIBE or REGISTER expires header
      static int jitterValue(int input, int lowerPercentage, int upperPercentage, int minimum=0);

      //in general content length handled automatically by SipMessage?
      static SipMessage* makeInvite(const NameAddr& target, const NameAddr& from);
      static SipMessage* makeInvite(const NameAddr& target, const NameAddr& from, const NameAddr& contact);
      static SipMessage* makeForwardedInvite(const SipMessage& invite);
      static void makeResponse(SipMessage& response, 
                               const SipMessage& request, 
                               int responseCode, 
                               const Data& reason = Data::Empty,
                               const Data& hostname = Data::Empty,
                               const Data& warning=Data::Empty);
      static void makeResponse(SipMessage& response, 
                               const SipMessage& request, 
                               int responseCode, 
                               const NameAddr& myContact, 
                               const Data& reason = Data::Empty,
                               const Data& hostname = Data::Empty,
                               const Data& warning=Data::Empty);
      static SipMessage* makeResponse(const SipMessage& request,
                                      int responseCode,
                                      const Data& reason = Data::Empty, 
                                      const Data& hostname = Data::Empty,
                                      const Data& warning=Data::Empty);
      static SipMessage* makeResponse(const SipMessage& request, 
                                      int responseCode, 
                                      const NameAddr& myContact, 
                                      const Data& reason = Data::Empty,
                                      const Data& hostname = Data::Empty,
                                      const Data& warning=Data::Empty);

      //to, maxforwards=70, requestLine& cseq method set, cseq sequence is 1
      //static SipMessage* makeRequest(const NameAddr& target, MethodTypes method); // deprecated

      //to, maxforward=70, requestline created, cseq method set, cseq sequence
      //is 1, from and from tag set, contact set, CallId created
      //while contact is only necessary for requests that establish a dialog,
      //those ar the requests most likely created by this method, others will
      //be generated by the dialog.
      static SipMessage* make405(const SipMessage& request,
                                 const int* allowedMethods = 0,
                                 int nMethods = -1);
        
      static SipMessage* makeRequest(const NameAddr& target, const NameAddr& from, const NameAddr& contact, MethodTypes method);
      static SipMessage* makeRequest(const NameAddr& target, const NameAddr& from, MethodTypes method);
      static SipMessage* makeCancel(const SipMessage& request);
      
      //creates to, from with tag, cseq method set, cseq sequence is 1
      static SipMessage* makeRegister(const NameAddr& to, const NameAddr& from, const NameAddr& contact);
      static SipMessage* makeRegister(const NameAddr& to, const NameAddr& from);
      static SipMessage* makeRegister(const NameAddr& to, const Data& transport, const NameAddr& contact);
      static SipMessage* makeRegister(const NameAddr& to, const Data& transport);
      static SipMessage* makeSubscribe(const NameAddr& target, const NameAddr& from, const NameAddr& contact);
      static SipMessage* makeSubscribe(const NameAddr& target, const NameAddr& from);
      static SipMessage* makeMessage(const NameAddr& target, const NameAddr& from, const NameAddr& contact);
      static SipMessage* makeMessage(const NameAddr& target, const NameAddr& from);
      static SipMessage* makePublish(const NameAddr& target, const NameAddr& from, const NameAddr& contact);
      static SipMessage* makePublish(const NameAddr& target, const NameAddr& from);
      static SipMessage* makeFailureAck(const SipMessage& request, const SipMessage& response);
      
      static Data computeUniqueBranch();
      static Data computeProxyBranch(const SipMessage& request);

      static Data computeCallId();
      static Data computeTag(int numBytes);

      enum AuthResult {Failed = 1, Authenticated, Expired, BadlyFormed};

      static AuthResult authenticateRequest(const SipMessage& request, 
                                            const Data& realm,
                                            const Data& password,
                                            int expiresDelta = 0);
      
      static std::pair<AuthResult,Data> 
                advancedAuthenticateRequest(const SipMessage& request, 
                                            const Data& realm,
                                            const Data& a1,
                                            int expiresDelta = 0);
      
      // create a 407 response with Proxy-Authenticate header filled in
      static SipMessage* makeProxyChallenge(const SipMessage& request, 
                                            const Data& realm,
                                            bool useAuth = true,
                                            bool stale = false);

      // adds authorization headers in reponse to the 401 or 407--currently
      // only supports md5.
      static SipMessage& addAuthorization(SipMessage& request,
                                          const SipMessage& challenge,
                                          const Data& username,
                                          const Data& password,
                                          const Data& cnonce,
                                          unsigned int& nonceCount);

      static Auth makeChallengeResponseAuth(SipMessage& request,
                                            const Data& username,
                                            const Data& password,
                                            const Auth& challenge,
                                            const Data& cnonce,
                                            unsigned int& nonceCount,
                                            Data& nonceCountString);      
      static Auth makeChallengeResponseAuthWithA1(const SipMessage& request,
                                                  const Data& username,
                                                  const Data& passwordHashA1,
                                                  const Auth& challenge,
                                                  const Data& cnonce,
                                                  unsigned int& nonceCount,
                                                  Data& nonceCountString);      

      static Data makeResponseMD5WithA1(const Data& a1,
                                        const Data& method, const Data& digestUri, const Data& nonce,
                                        const Data& qop = Data::Empty, const Data& cnonce = Data::Empty, 
                                        const Data& cnonceCount = Data::Empty, const Contents *entityBody = 0);

      static Data makeResponseMD5(const Data& username, const Data& password, const Data& realm, 
                                  const Data& method, const Data& digestUri, const Data& nonce,
                                  const Data& qop = Data::Empty, const Data& cnonce = Data::Empty, 
                                  const Data& cnonceCount = Data::Empty, const Contents *entityBody = 0);
      
      
      static Data makeNonce(const SipMessage& request, const Data& timestamp);

      static Uri makeUri(const Data& aor, const Data& scheme=Symbols::DefaultSipScheme);

      static void processStrictRoute(SipMessage& request);

      // renamed to make more explicit that this is the port that we should reply too
      // given that we are following SIP rules WRT rport etc.
      static int getPortForReply(SipMessage& request);

      static Uri fromAor(const Data& aor, const Data& scheme=Symbols::DefaultSipScheme);

      // Do basic checks to validate a received message off the wire
      static bool validateMessage(const SipMessage& message);

      // GRUU support -- reversibly and opaquely combine instance id and aor
      static Data gruuUserPart(const Data& instanceId,
                               const Data& aor,
                               const Data& key);

      // GRUU support -- extract instance id and aor from user portion
      static std::pair<Data,Data> fromGruuUserPart(const Data& gruuUserPart,
                                                   const Data& key);

      struct ContentsSecAttrs
      {
            ContentsSecAttrs();
            ContentsSecAttrs(std::auto_ptr<Contents> contents,
                             std::auto_ptr<SecurityAttributes> attributes);
            ContentsSecAttrs(const ContentsSecAttrs& rhs);
            ContentsSecAttrs& operator=(const ContentsSecAttrs& rhs);
            mutable std::auto_ptr<Contents> mContents;
            mutable std::auto_ptr<SecurityAttributes> mAttributes;
      };

      static ContentsSecAttrs extractFromPkcs7(const SipMessage& message, Security& security);

      
      enum FailureMessageEffect{ DialogTermination, TransactionTermination, UsageTermination, 
                                 RetryAfter, OptionalRetryAfter, ApplicationDependant };
      
      static FailureMessageEffect determineFailureMessageEffect(const SipMessage& response);      

   private:
      static Data qopOption(const Auth& challenge);
};

}

#endif

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by Vovida
 * Networks, Inc. and many individuals on behalf of Vovida Networks,
 * Inc.  For more information on Vovida Networks, Inc., please see
 * <http://www.vovida.org/>.
 *
 */