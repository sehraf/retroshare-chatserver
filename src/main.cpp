#include <retroshare/rsiface.h>     /* definition of iface */
#include <retroshare/rsinit.h>      /* definition of iface */
#include <retroshare/rsidentity.h>
#include <retroshare/rsconfig.h> 
#include <rsserver/rsaccounts.h>
#include <iostream>
#include "chatserver.h"
#include "MinimalNotify.h"
#include <dirent.h>
#include <ctime>
#include <unistd.h>

// copied from http://bytes.com/topic/c/answers/584434-check-directory-exists-c
bool DirectoryExists( const char* pzPath )
{
    if ( pzPath == NULL) return false;

    DIR *pDir;
    bool bExists = false;

    pDir = opendir (pzPath);

    if (pDir != NULL)
    {
        bExists = true;
        (void) closedir (pDir);
    }

    return bExists;
}

bool file_writable(const char * filename)
{
	if (FILE * file = fopen(filename, "rw"))
	{
		fclose(file);
		return true;
	}
	return false;
}

int generateGxsId(const std::string& name)
{
	uint32_t token;
	RsIdentityParameters params;
	// signing seems not to work from noguis
	// if you want to use a signed ID create it by hand
	params.isPgpLinked = false;
	params.nickname = name;
	rsIdentity->createIdentity(token, params);

	// waiting for max. 10 seconds
	uint counter = 0;
	while (rsIdentity->getTokenService()->requestStatus(token) != RsTokenService::GXS_REQUEST_V2_STATUS_COMPLETE && counter++ < 10)
		sleep(1);

	// request fulfilled or timeout?
	if(rsIdentity->getTokenService()->requestStatus(token) != RsTokenService::GXS_REQUEST_V2_STATUS_COMPLETE)
	{
		std::cerr << "Error: can't generate GXS Id" << std::endl;
		return 1;
	}
	return 0;
}

int generateAndSetId(const std::string &name, RsGxsId &id)
{
	if(generateGxsId(name) != 0)
		// geneation failed and error was already printed -> just return
		return 1;

	// pick first ID that is the newly generated one
	std::list<RsGxsId> ids;
	rsIdentity->getOwnIds(ids);
	id = ids.front();
	return 0;
}

int main(int argc, char **argv)
{
	if (!DirectoryExists(certificatePath.c_str()))
	{
		std::cout << "hardcoded certificatePath " << certificatePath << " doesn't exist!" << std::endl;
		return 1;
	}
	if (!file_writable(storagePath.c_str()))
	{
		std::cout << "hardcoded storagePath " << storagePath << " doesn't exist or isn't writable!" << std::endl;
		return 1;
	}
	if (!file_writable(temporaryFriendsFile.c_str()))
	{
		std::cout << "hardcoded temporary friends file " << temporaryFriendsFile << " doesn't exist or isn't writable!" << std::endl;
		return 1;
	}

	// startup libretroshare
	RsInit::InitRsConfig();
	int initResult = RsInit::InitRetroShare(argc, argv, true);
	if (initResult < 0)
	{
		/* Error occured */
		switch (initResult)
		{
			case RS_INIT_AUTH_FAILED:
				std::cerr << "RsInit::InitRetroShare AuthGPG::InitAuth failed" << std::endl;
				break;
			default:
				/* Unexpected return code */
				std::cerr << "RsInit::InitRetroShare unexpected return code " << initResult << std::endl;
				break;
		};
		return 1;
	}


	RsControl::earlyInitNotificationSystem() ;
	NotifyTxt *notify = new NotifyTxt() ;
	rsNotify->registerNotifyClient(notify);


	RsPeerId preferredId;
	RsPgpId gpgId;
	std::string gpgName, gpgEmail, sslName;

	RsAccounts::GetPreferredAccountId(preferredId);

	if (RsAccounts::GetAccountDetails(preferredId, gpgId, gpgName, gpgEmail, sslName))
	{
		rsAccounts->SelectPGPAccount(gpgId);
	}

	/* Key + Certificate are loaded into libretroshare */

	std::string error_string ;
	int retVal = RsInit::LockAndLoadCertificates(false,error_string);
	switch(retVal)
	{
		case 0:	break;
		case 1:	std::cerr << "Error: another instance of retroshare is already using this profile" << std::endl;
				return 1;
		case 2: std::cerr << "An unexpected error occurred while locking the profile" << std::endl;
				return 1;
		case 3: std::cerr << "An error occurred while login with the profile" << std::endl;
				return 1;
		default: std::cerr << "Main: Unexpected switch value " << retVal << std::endl;
				return 1;
	}

	/* Start-up libretroshare server threads */
	RsControl::instance() -> StartupRetroShare();

	/* Disable all Turtle Routing and tunnel requests */
	rsConfig->setOperatingMode(RS_OPMODE_NOTURTLE);

	// set up GXS ID
	RsGxsId gxsId(cs_gxsId);
	if(!gxsId.isNull()) {
		// we have an ID - still need to verify that it actualy exists
		std::list<RsGxsId> ids;
		rsIdentity->getOwnIds(ids);

		// wait for max. 60 seconds
		int16_t counter = 0;
		while(ids.empty() && counter++ < 12)
		{
			sleep(5);
			rsIdentity->getOwnIds(ids);
		}

		if(ids.empty()) {
			std::cerr << "Error: unabled to find any GXS ID" << std::endl;
			return 1;
		}

		std::cout << "searching for GXS ID " << gxsId.toStdString() << std::endl;
		bool found = false;
		std::list<RsGxsId>::iterator it;
		for(it = ids.begin(); it != ids.end(); ++it) {
			std::cout << "GXS ID: " << it->toStdString() << " - ";
			if(*it == gxsId) {
				std::cout << "OK!" << std::endl;
				found = true;
				break;
			}
			std::cout << "no..." << std::endl;
		}

		if(!found) {
			std::cerr << "Error: unabled to find correct GXS ID (" << gxsId.toStdString() << ")" << std::endl;
			return 1;
		}
	} else { // if(!id.isNull())
		// no ID given -> pick one based in the nickname
		gxsId.clear();
		std::list<RsGxsId> ids;
		rsIdentity->getOwnIds(ids);

		// wait for max. 60 seconds
		int16_t counter = 0;
		while(ids.empty() && counter++ < 12)
		{
			sleep(5);
			rsIdentity->getOwnIds(ids);
		}

		if(ids.empty()) {
			if(!cs_genNewId) {
				std::cerr << "Error: unable to find any GXS ID and generation is turned off" << std::endl;
				return 1;
			}

			std::cout << "no GXS ID found -> generating a new one" << std::endl;
			if(!generateAndSetId(cs_nickname, gxsId))
				return 1;
		} else { //if(ids.empty())
			// find gxs id with suitable name
			std::list<RsGxsId>::iterator it;
			RsIdentityDetails details;
			for(it = ids.begin(); it != ids.end(); ++it) {
				// slightly ugly but needed since the details might not be available on the first request
				// first request
				rsIdentity->getIdDetails(*it, details);
				// wait
				sleep(2);
				// second request
				rsIdentity->getIdDetails(*it, details);
				std::cout << "GXS ID: " << details.mNickname;
				if(details.mNickname == cs_nickname) {
					gxsId = *it;
					std::cout << " - choosing this one: " << gxsId.toStdString() << std::endl;
					break;
				}
				std::cout << std::endl;
			}

			if(gxsId.isNull()) {
				if(!cs_genNewId) {
					std::cerr << "Error: no suitable GXS ID found and generation is turned off" << std::endl;
					return 1;
				}

				if(!generateAndSetId(cs_nickname, gxsId))
					return 1;
			}
			// ready to go
		} // if(ids.empty())
	} // if(!id.isNull())

	// last sanity check - should not occur but you never know ...
	if(gxsId.isNull())
	{
		std::cerr << "Error: can't find/generate suitable GXS Id" << std::endl;
		return 1;
	}
	rsMsgs->setDefaultIdentityForChatLobby(gxsId);

	// start chatserver
	Chatserver *chatserver = new Chatserver(gxsId);
	while (true)
	{
			sleep(1);
			chatserver->tick();
	}
	return 0;
}

