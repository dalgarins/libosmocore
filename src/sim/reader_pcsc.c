#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <osmocom/core/talloc.h>
#include <osmocom/sim/sim.h>

#include <PCSC/wintypes.h>
#include <PCSC/winscard.h>

#include "sim_int.h"

#define PCSC_ERROR(rv, text) \
if (rv != SCARD_S_SUCCESS) { \
	fprintf(stderr, text ": %s (0x%lX)\n", pcsc_stringify_error(rv), rv); \
	goto end; \
} else { \
        printf(text ": OK\n\n"); \
}



struct pcsc_reader_state {
	SCARDCONTEXT hContext;
	SCARDHANDLE hCard;
	DWORD dwActiveProtocol;
	const SCARD_IO_REQUEST *pioSendPci;
	SCARD_IO_REQUEST pioRecvPci;
	char *name;
};

static struct osim_reader_hdl *pcsc_reader_open(int num, const char *id, void *ctx)
{
	struct osim_reader_hdl *rh;
	struct pcsc_reader_state *st;
	long rc;
	LPSTR mszReaders = NULL;
	DWORD dwReaders;
	unsigned int num_readers;
	char *ptr;

	/* FIXME: implement matching on id or num */

	rh = talloc_zero(ctx, struct osim_reader_hdl);
	st = rh->priv = talloc_zero(rh, struct pcsc_reader_state);

	rc = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL,
				   &st->hContext);
	if (rc != SCARD_S_SUCCESS)
		goto end;

	dwReaders = SCARD_AUTOALLOCATE;
	rc = SCardListReaders(st->hContext, NULL, (LPSTR)&mszReaders, &dwReaders);
	PCSC_ERROR(rc, "SCardListReaders");

	num_readers = 0;
	ptr = mszReaders;
	while (*ptr != '\0') {
		ptr += strlen(ptr)+1;
		num_readers++;
	}

	if (num_readers == 0)
		goto end;

	st->name = talloc_strdup(rh, mszReaders);
	st->dwActiveProtocol = -1;

	return rh;
end:
	talloc_free(rh);
	return NULL;
}

static struct osim_card_hdl *pcsc_card_open(struct osim_reader_hdl *rh)
{
	struct pcsc_reader_state *st = rh->priv;
	struct osim_card_hdl *card;
	struct osim_chan_hdl *chan;
	int rc;

	rc = SCardConnect(st->hContext, st->name, SCARD_SHARE_SHARED,
			  SCARD_PROTOCOL_T0, &st->hCard, &st->dwActiveProtocol);
	PCSC_ERROR(rc, "SCardConnect");

	st->pioSendPci = SCARD_PCI_T0;

	card = talloc_zero(rh, struct osim_card_hdl);
	INIT_LLIST_HEAD(&card->channels);
	card->reader = rh;
	rh->card = card;

	/* create a default channel */
	chan = talloc_zero(card, struct osim_chan_hdl);
	chan->card = card;
	llist_add(&chan->list, &card->channels);

	return card;

end:
	return NULL;
}


static int pcsc_transceive(struct osim_reader_hdl *rh, struct msgb *msg)
{
	struct pcsc_reader_state *st = rh->priv;
	DWORD rlen = msgb_tailroom(msg);
	int rc;

	printf("TX: %s\n", osmo_hexdump(msg->data, msg->len));

	rc = SCardTransmit(st->hCard, st->pioSendPci, msg->data, msgb_length(msg),
			   &st->pioRecvPci, msg->tail, &rlen);
	PCSC_ERROR(rc, "SCardEndTransaction");

	printf("RX: %s\n", osmo_hexdump(msg->tail, rlen));
	msgb_put(msg, rlen);
	msgb_apdu_le(msg) = rlen;

	return 0;
end:
	return -EIO;
}

const struct osim_reader_ops pcsc_reader_ops = {
	.name = "PC/SC",
	.reader_open = pcsc_reader_open,
	.card_open = pcsc_card_open,
	.transceive = pcsc_transceive,
};

